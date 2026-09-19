// license:BSD-3-Clause
// copyright-holders:nanocode
/***************************************************************************

    Flying Pig Systems Wholehog II  ("TNT" processor Mk3, 1997)

    Lighting control console. Hardware from the Processor Mk3 schematics and
    memory map published by the Flying Pig Systems Archive:

      CPU        Intel i960CF @ 50 MHz
      boot ROM   27C512 64K x 8            region F  (F0000000)  (not dumped: replaced by a synthetic ROM, see below)
      fast SRAM  4 x HM62832  128 KB        region 2  (20000000)  battery backed, holds the OS stack
      main RAM   8 x HM658512 4 MB PSRAM    region 4  (40000000)  battery backed, OS data from 40000000
      flash      8 x N28F020  2 MB          region 5  (50000000)  console OS + fixture "ROM Library"
      super I/O  FDC37C652 floppy / printer / serial 3F8 / link 2F8     region 3, IBM AT layout
      PLD regs   keyboard (AT keyboard on KCLK/KDATA), TXRDY/RXRDY, control  region 1
      VGA 1/2    2 x Cirrus Logic GD5426, 1 MB each   I/O region 8 (87/8F), memory region 9 (97/9F)
      LCD 1/2    2 x Chips & Technologies F65510      I/O region A (A7/AF), memory region B (B7/BF)
      LTC        DAK010 timecode reader               region C
      UARTs      6 x 16C550: P0 MIDI (D5), P1 DMX in (D7), P2-P4 (D9 DB DD), P5 expansion (DF)
      DMX out    4 x IDT72125 FIFOs (E7 E9 EB ED) clocked out serially by the PLD at 250 kbit/s

    Boot ROM.  The 27C512 is not dumped.  The OS image relies on it for two things, both reconstructed here:
      * the i960 initial boot record, stack and the system procedure table used by the OS's "calls N" wrappers
        (0x501393a0..0x50139478 in v3.3 b177).  Each table entry points to a stub in the synthetic ROM that writes
        to a trap register; the driver services the call in C++ with the CPU's g0-g3 as arguments and g0 as result.
      * interrupt dispatch: calls 3 (vector, handler+4, 0) installs a handler for an i960 vector (0x12 keyboard,
        0x22 serial, 0x32 link/front panel, 0x62 expansion UART, 0x72 MIDI, 0x82 LTC, 0xc2 DMX input, 0xd2 tick timer),
        calls 9/10 (pin) enable/disable an XINT pin (7 = timer), calls 7/8 enable/disable interrupts globally.

    DMX output.  The OS writes 16-bit words to the FIFOs: 0000 then C000 for the break/mark-after-break and
    (byte << 4) | F007 for every slot (start bit, 8 data bits, stop bits, LSB shifted out first).  The driver decodes
    this back to DMX frames and transmits them as Art-Net ArtDMX (UDP 6454), one universe per port.
      HOG2_ARTNET_HOST      comma separated destination hosts, default 127.0.0.1 (a .255 address enables broadcast)
      HOG2_ARTNET_PORT      default 6454
      HOG2_ARTNET_UNIVERSE  15-bit Art-Net port address of DMX port 1, default 0 (ports 2-4 follow)

    The two floppy images of a software release ("tnt_177", v3.3 b177) are:
      disk1/hog.bin  [type 2][disk 0][len 0x15E000] + first 0x15E000 bytes of the flash image (runs in place at 50000000) + CRC
      disk2/hog.bin  [type 2][disk 1][len 0x0A1F00] + the rest of the image (data, vtables, fixture "ROM Library") + CRC
    The boot EPROM normally copies these from floppy into flash; here the flash is preloaded from the files.

***************************************************************************/

#include "emu.h"
#include "cpu/i960/i960.h"
#include "machine/ins8250.h"
#include "machine/fdc37c665gt.h"
#include "imagedev/floppy.h"
#include "formats/pc_dsk.h"
#include "machine/nvram.h"
#include "machine/pckeybrd.h"
#include "video/pc_vga_cirrus.h"
#include "video/pc_vga_chips.h"
#include "bus/rs232/rs232.h"
#include "bus/rs232/null_modem.h"
#include "screen.h"

#include "wholehog2.lh"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define LOG_SYSCALL (1U << 1)
#define LOG_DMX     (1U << 2)
#define LOG_LINK    (1U << 3)
#define LOG_FDC     (1U << 4)
#define VERBOSE (1U << 0)
#include "logmacro.h"

namespace {

// ------------------------------------------------------------------ Art-Net sender

class artnet_sender
{
public:
	artnet_sender() = default;
	~artnet_sender() { close_socket(); }

	void open()
	{
		close_socket();
		const char *env_host = std::getenv("HOG2_ARTNET_HOST");
		const char *env_port = std::getenv("HOG2_ARTNET_PORT");
		const char *env_uni = std::getenv("HOG2_ARTNET_UNIVERSE");
		m_port = env_port ? std::atoi(env_port) : 6454;
		m_universe = env_uni ? std::atoi(env_uni) : 0;
		std::string hosts = env_host ? env_host : "127.0.0.1";

#ifdef _WIN32
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
		m_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (m_sock < 0)
			return;
		int one = 1;
		::setsockopt(m_sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&one), sizeof(one));

		m_targets.clear();
		size_t pos = 0;
		while (pos <= hosts.size())
		{
			size_t comma = hosts.find(',', pos);
			if (comma == std::string::npos) comma = hosts.size();
			std::string host = hosts.substr(pos, comma - pos);
			pos = comma + 1;
			if (host.empty()) continue;
			sockaddr_in sa{};
			sa.sin_family = AF_INET;
			sa.sin_port = htons(m_port);
			if (::inet_pton(AF_INET, host.c_str(), &sa.sin_addr) == 1)
				m_targets.push_back(sa);
		}
		m_seq.assign(4, 0);
	}

	bool ok() const { return m_sock >= 0 && !m_targets.empty(); }
	int universe() const { return m_universe; }

	void send_dmx(int port, const uint8_t *slots, int count)
	{
		if (!ok()) return;
		uint8_t pkt[18 + 512];
		std::memcpy(pkt, "Art-Net\0", 8);
		pkt[8] = 0x00; pkt[9] = 0x50;              // OpDmx (little endian)
		pkt[10] = 0; pkt[11] = 14;                 // protocol version 14
		uint8_t &seq = m_seq[port & 3];
		seq = seq ? (seq & 0xff) : 1;
		pkt[12] = seq++;                            // sequence 1..255
		if (seq == 0) seq = 1;
		pkt[13] = port + 1;                         // physical port
		const int addr = (m_universe + port) & 0x7fff;
		pkt[14] = addr & 0xff;                      // SubUni
		pkt[15] = (addr >> 8) & 0x7f;               // Net
		int len = std::min(std::max(count, 2), 512);
		if (len & 1) len++;
		pkt[16] = len >> 8; pkt[17] = len & 0xff;
		std::memset(pkt + 18, 0, 512);
		std::memcpy(pkt + 18, slots, std::min(count, 512));
		for (auto &t : m_targets)
			::sendto(m_sock, reinterpret_cast<const char *>(pkt), 18 + len, 0, reinterpret_cast<const sockaddr *>(&t), sizeof(t));
	}

private:
	void close_socket()
	{
		if (m_sock >= 0)
		{
#ifdef _WIN32
			::closesocket(m_sock);
#else
			::close(m_sock);
#endif
		}
		m_sock = -1;
	}

	int m_sock = -1;
	int m_port = 6454;
	int m_universe = 0;
	std::vector<sockaddr_in> m_targets;
	std::vector<uint8_t> m_seq;
};

} // anonymous namespace

// ------------------------------------------------------------------ front panel (87C51 board on the LINK port)
//
// 9600 8N1.  Both directions carry 4-byte packets [cmd][b1][b2][chk] with (cmd+b1+b2+chk) & 0xff == 0.
// Console -> panel (from the OS's panel driver at 0x50135dd4..0x50136290):
//   07 09 00     init / probe          07 ff 00  run mode           00 00 00  sync
//   01 00 00     identify request      06 00 01  enable             05 xx yy  LED xx = yy
//   08 .. ..  09 .. ..                 (display / misc)
// Panel -> console:
//   raw 0x01 byte while the console is searching (sets the "found" flag)
//   01 80 tt     identify reply, tt = panel type (non zero)
//   05 b1 b2     key: index = b1*8 + (b2 & 7), b2 bit 7 = pressed
//   07 wheel d   wheel 0-4, signed delta
//   08 a  v      touch: a = screen*2 + axis (0 = x, 1 = y), 8-bit value; the y byte completes a touch event
//   0a fader v   fader 0..n, 8-bit value
//   02 / 03      stop / resume console transmission,  04 request resync

class hog2_panel_device;
DECLARE_DEVICE_TYPE(HOG2_PANEL, hog2_panel_device)

class hog2_panel_device : public device_t, public device_rs232_port_interface, public device_serial_interface
{
public:
	hog2_panel_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0)
		: device_t(mconfig, HOG2_PANEL, tag, owner, clock)
		, device_rs232_port_interface(mconfig, *this)
		, device_serial_interface(mconfig, *this)
		, m_keys(*this, "KEYS%u", 0U)
		, m_faders(*this, "FADER%u", 0U)
		, m_wheels(*this, "WHEEL%u", 0U)
		, m_touch_x(*this, "TOUCH_X")
		, m_touch_y(*this, "TOUCH_Y")
		, m_touch_btn(*this, "TOUCH_BTN")
		, m_leds(*this, "panel_led%u", 0U)
	{ }

	virtual void input_txd(int state) override { device_serial_interface::rx_w(state); }

	// pointer position for the LCD renderer: returns true when the pointer is over LCD 'screen'
	bool pointer(int screen, int &x, int &y, bool &down) const
	{
		x = m_touch_px; y = m_touch_py; down = m_touch_down;
		return screen == m_touch_screen;
	}

protected:
	virtual ioport_constructor device_input_ports() const override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void tra_callback() override { output_rxd(transmit_register_get_data_bit()); }
	virtual void tra_complete() override { m_tx_busy = false; send_next(); }
	virtual void rcv_complete() override;

private:
	TIMER_CALLBACK_MEMBER(scan);
	void queue_byte(uint8_t b) { m_tx.push_back(b); send_next(); }
	void queue_packet(uint8_t cmd, uint8_t b1, uint8_t b2)
	{
		m_tx.push_back(cmd); m_tx.push_back(b1); m_tx.push_back(b2); m_tx.push_back((0x100 - (cmd + b1 + b2)) & 0xff);
		send_next();
	}
	void send_next()
	{
		if (m_tx_busy || m_tx.empty()) return;
		m_tx_busy = true;
		transmit_register_setup(m_tx.front());
		m_tx.pop_front();
	}
	void handle_packet(const uint8_t *p);
	void report_faders()
	{
		for (int i = 0; i < 10; i++)
			queue_packet(0x06, i, m_fader_state[i]);
	}

	required_ioport_array<13> m_keys;
	required_ioport_array<10> m_faders;
	required_ioport_array<4> m_wheels;
	required_ioport m_touch_x;
	required_ioport m_touch_y;
	required_ioport m_touch_btn;
	output_finder<64> m_leds;

	emu_timer *m_scan_timer = nullptr;
	std::deque<uint8_t> m_tx;
	bool m_tx_busy = false;
	uint8_t m_rx[4] = {};
	int m_rx_pos = 0;
	bool m_online = false;
	int m_fader_report = 0;
	uint8_t m_key_state[13] = {};
	uint8_t m_fader_state[10] = {};
	uint8_t m_wheel_last[4] = {};
	bool m_touch_down = false;
	int m_touch_screen = 0;         // LCD under the pointer (0/1)
	int m_touch_px = 0, m_touch_py = 0;   // pointer position on that LCD in pixels
};

DEFINE_DEVICE_TYPE(HOG2_PANEL, hog2_panel_device, "hog2_panel", "Wholehog II front panel")

// Panel key numbering (key = port*8 + bit) follows the OS key table at flash 0x5016be50; names come from the command
// line echoes of each key.  Unnamed keys did not produce visible output on the command line.
static INPUT_PORTS_START( hog2_panel )
	PORT_START("KEYS0")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 1 Go") PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 2 Go") PORT_CODE(KEYCODE_W)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 3 Go") PORT_CODE(KEYCODE_E)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 4 Go") PORT_CODE(KEYCODE_R)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 5 Go") PORT_CODE(KEYCODE_T)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 6 Go") PORT_CODE(KEYCODE_Y)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 7 Go") PORT_CODE(KEYCODE_U)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 8 Go") PORT_CODE(KEYCODE_I)
	PORT_START("KEYS1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 1 Halt")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 2 Halt")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 3 Halt")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 4 Halt")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 5 Halt")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 6 Halt")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 7 Halt")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 8 Halt")
	PORT_START("KEYS2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 1 Flash")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 2 Flash")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 3 Flash")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 4 Flash")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 5 Flash")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 6 Flash")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 7 Flash")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 8 Flash")
	PORT_START("KEYS3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 1 Choose")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 2 Choose")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 3 Choose")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 4 Choose")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 5 Choose")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 6 Choose")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 7 Choose")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 8 Choose")
	PORT_START("KEYS4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 32")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 33")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 34")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 35")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Group") PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Position") PORT_CODE(KEYCODE_F2)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Colour") PORT_CODE(KEYCODE_F3)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Beam") PORT_CODE(KEYCODE_F4)
	PORT_START("KEYS5")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 1 List")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 2 List")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 3 List")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 4 List")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 5 List")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 6 List")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 7 List")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 8 List")
	PORT_START("KEYS6")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 1")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 2")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 3")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 4")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 5")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 6")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 7")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 8")
	PORT_START("KEYS7")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 9 List")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Master 10 List")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 9")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Window control 10")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 60")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Next") PORT_CODE(KEYCODE_N)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 62")
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 63")
	PORT_START("KEYS8")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Copy") PORT_CODE(KEYCODE_C)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Move") PORT_CODE(KEYCODE_M)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Delete") PORT_CODE(KEYCODE_D)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 67")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Backspace")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("/")
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("-") PORT_CODE(KEYCODE_MINUS_PAD)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("+") PORT_CODE(KEYCODE_PLUS_PAD)
	PORT_START("KEYS9")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Active") PORT_CODE(KEYCODE_A)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Load") PORT_CODE(KEYCODE_L)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 74")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Record") PORT_CODE(KEYCODE_R)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("7") PORT_CODE(KEYCODE_7_PAD)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("8") PORT_CODE(KEYCODE_8_PAD)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("9") PORT_CODE(KEYCODE_9_PAD)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Thru") PORT_CODE(KEYCODE_SLASH_PAD)
	PORT_START("KEYS10")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 80")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Goto") PORT_CODE(KEYCODE_G)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 82")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 83")
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 84")
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("0") PORT_CODE(KEYCODE_0_PAD)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME(".") PORT_CODE(KEYCODE_DEL_PAD)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 87")
	PORT_START("KEYS11")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Macro") PORT_CODE(KEYCODE_F5)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Page") PORT_CODE(KEYCODE_F6)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 90")
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("List") PORT_CODE(KEYCODE_F8)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("4") PORT_CODE(KEYCODE_4_PAD)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("5") PORT_CODE(KEYCODE_5_PAD)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("6") PORT_CODE(KEYCODE_6_PAD)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Full") PORT_CODE(KEYCODE_ENTER_PAD)
	PORT_START("KEYS12")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 96")
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Panel key 97")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Clear") PORT_CODE(KEYCODE_BACKSPACE)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("Set") PORT_CODE(KEYCODE_EQUALS)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("1") PORT_CODE(KEYCODE_1_PAD)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("2") PORT_CODE(KEYCODE_2_PAD)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("3") PORT_CODE(KEYCODE_3_PAD)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("@") PORT_CODE(KEYCODE_ASTERISK)

	PORT_START("FADER0") PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Fader 1")  PORT_PLAYER(1)
	PORT_START("FADER1") PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Fader 2")  PORT_PLAYER(2)
	PORT_START("FADER2") PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Fader 3")  PORT_PLAYER(3)
	PORT_START("FADER3") PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Fader 4")  PORT_PLAYER(4)
	PORT_START("FADER4") PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Fader 5")  PORT_PLAYER(5)
	PORT_START("FADER5") PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Fader 6")  PORT_PLAYER(6)
	PORT_START("FADER6") PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Fader 7")  PORT_PLAYER(7)
	PORT_START("FADER7") PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Fader 8")  PORT_PLAYER(8)
	PORT_START("FADER8") PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Fader 9 (crossfader)")  PORT_PLAYER(9)
	PORT_START("FADER9") PORT_BIT(0xff, 0xff, IPT_PADDLE) PORT_SENSITIVITY(25) PORT_KEYDELTA(8) PORT_NAME("Grand Master (A)") PORT_PLAYER(10)

	PORT_START("WHEEL0") PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_SENSITIVITY(25) PORT_KEYDELTA(4) PORT_NAME("Wheel 1") PORT_PLAYER(1)
	PORT_START("WHEEL1") PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_SENSITIVITY(25) PORT_KEYDELTA(4) PORT_NAME("Wheel 2") PORT_PLAYER(2)
	PORT_START("WHEEL2") PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_SENSITIVITY(25) PORT_KEYDELTA(4) PORT_NAME("Wheel 3") PORT_PLAYER(3)
	PORT_START("WHEEL3") PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_SENSITIVITY(25) PORT_KEYDELTA(4) PORT_NAME("Wheel 4") PORT_PLAYER(4)

	// One pointer over both LCDs laid side by side (0..1279 x 0..479). The layout script (wholehog2.lay) feeds it from
	// MAME pointer events in view coordinates, so any window shape works; cursor keys move it too.
	// the driver draws it as a cross on the LCD it is over, button 1 is the finger.
	PORT_START("TOUCH_X") PORT_BIT(0xfff, 0x400, IPT_LIGHTGUN_X) PORT_MINMAX(0, 0xfff) PORT_SENSITIVITY(100) PORT_KEYDELTA(24) PORT_NAME("Touch pointer X") PORT_CODE_INC(KEYCODE_RIGHT) PORT_CODE_DEC(KEYCODE_LEFT)
	PORT_START("TOUCH_Y") PORT_BIT(0xfff, 0x800, IPT_LIGHTGUN_Y) PORT_MINMAX(0, 0xfff) PORT_SENSITIVITY(100) PORT_KEYDELTA(24) PORT_NAME("Touch pointer Y") PORT_CODE_INC(KEYCODE_DOWN) PORT_CODE_DEC(KEYCODE_UP)
	PORT_START("TOUCH_BTN")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_NAME("Touch") PORT_CODE(KEYCODE_RCONTROL)
INPUT_PORTS_END

ioport_constructor hog2_panel_device::device_input_ports() const { return INPUT_PORTS_NAME(hog2_panel); }

void hog2_panel_device::device_start()
{
	m_scan_timer = timer_alloc(FUNC(hog2_panel_device::scan), this);
	save_item(NAME(m_tx_busy));
	save_item(NAME(m_rx));
	save_item(NAME(m_rx_pos));
	save_item(NAME(m_online));
	save_item(NAME(m_key_state));
	save_item(NAME(m_fader_state));
	save_item(NAME(m_wheel_last));
	save_item(NAME(m_touch_down));
	save_item(NAME(m_touch_screen));
	save_item(NAME(m_touch_px));
	save_item(NAME(m_touch_py));
}

void hog2_panel_device::device_reset()
{
	set_data_frame(1, 8, PARITY_NONE, STOP_BITS_1);
	set_tra_rate(9600);
	set_rcv_rate(9600);
	output_rxd(1);
	output_dcd(0);
	output_dsr(0);
	output_cts(0);
	m_tx.clear();
	m_tx_busy = false;
	m_rx_pos = 0;
	m_online = false;
	for (int i = 0; i < 13; i++) m_key_state[i] = 0;
	for (int i = 0; i < 10; i++) m_fader_state[i] = m_faders[i]->read();
	for (int i = 0; i < 4; i++) m_wheel_last[i] = m_wheels[i]->read();
	m_scan_timer->adjust(attotime::from_msec(10), 0, attotime::from_msec(10));
}

void hog2_panel_device::rcv_complete()
{
	receive_register_extract();
	m_rx[m_rx_pos++] = get_received_char();
	if (m_rx_pos == 4)
	{
		m_rx_pos = 0;
		if (((m_rx[0] + m_rx[1] + m_rx[2] + m_rx[3]) & 0xff) != 0)
		{
			logerror("panel: bad checksum %02x %02x %02x %02x\n", m_rx[0], m_rx[1], m_rx[2], m_rx[3]);
			// resynchronise on the next zero (sync) byte
			m_rx[0] = m_rx[1]; m_rx[1] = m_rx[2]; m_rx[2] = m_rx[3]; m_rx_pos = 3;
			return;
		}
		handle_packet(m_rx);
	}
}

void hog2_panel_device::handle_packet(const uint8_t *p)
{
	switch (p[0])
	{
	case 0x00:  // sync
		break;
	case 0x01:  // identify request
		if (!(p[1] & 0x80))
			queue_packet(0x01, 0x80, 0x01);
		break;
	case 0x04:  // resync request: the console clears its "found" flag here and expects the hello sequence again
		m_online = false;
		// four zero bytes satisfy the console's sync counter, then the 01 sets "found" and, together with the
		// three bytes that follow, forms the identify packet the console parses next
		for (int i = 0; i < 4; i++) queue_byte(0x00);
		queue_packet(0x01, 0x80, 0x01);
		break;
	case 0x05:  // LED
		m_leds[p[1] & 63] = p[2];
		break;
	case 0x06:  // fader reporting enable: answer with every fader position, again 1.5 s and 4 s later (the first
		//         request arrives while the OS still drops input events)
		if (p[2])
		{
			report_faders();
			m_fader_report = 400;
		}
		break;
	case 0x07:  // mode: 09 = init (also the console's probe while searching), ff = run
		if (p[1] == 0x09)
			m_online = false;
		else if (p[1] == 0xff)
			m_online = true;
		break;
	default:
		break;   // 08/09: display and misc commands, ignored
		break;
	}
}

TIMER_CALLBACK_MEMBER(hog2_panel_device::scan)
{
	// keys
	for (int port = 0; port < 13; port++)
	{
		const uint8_t now = m_keys[port]->read();
		const uint8_t diff = now ^ m_key_state[port];
		if (!diff) continue;
		m_key_state[port] = now;
		for (int bit = 0; bit < 8; bit++)
			if (BIT(diff, bit))
				queue_packet(0x05, port, bit | (BIT(now, bit) ? 0x00 : 0x80));   // bit 7 set = key released (the OS starts auto-repeat on it)
	}
	// faders: cmd 6 = fader position (OS posts events F4/F1/F2 for masters/xfader/GM)
	for (int i = 0; i < 10; i++)
	{
		const uint8_t v = m_faders[i]->read();
		if (v != m_fader_state[i])
		{
			m_fader_state[i] = v;
			queue_packet(0x06, i, v);
		}
	}
	if (m_fader_report)
	{
		m_fader_report--;
		if (m_fader_report == 250 || m_fader_report == 0)
			report_faders();
	}
	// wheels: relative
	for (int i = 0; i < 4; i++)
	{
		const uint8_t v = m_wheels[i]->read();
		const int8_t delta = int8_t(v - m_wheel_last[i]);
		if (delta)
		{
			m_wheel_last[i] = v;
			queue_packet(0x07, i, uint8_t(delta));
		}
	}
	// touch: the pointer covers both LCDs (1280 x 480); the LCD under it receives 8-bit raw coordinates, streamed
	// while the finger is down, and a full-scale pair (which the OS maps outside the display) when it lifts
	{
		const int xt = m_touch_x->read() * 1280 / 0x1000, yt = m_touch_y->read() * 480 / 0x1000;
		const bool down = BIT(m_touch_btn->read(), 0);
		if (!m_touch_down)
		{
			// the pointer may move between LCDs only while lifted, so a press always belongs to one LCD
			m_touch_screen = xt >= 640 ? 1 : 0;
		}
		m_touch_px = std::clamp(xt - m_touch_screen * 640, 0, 639);
		m_touch_py = std::clamp(yt, 0, 479);
		if (down)
		{
			queue_packet(0x08, m_touch_screen * 2 + 0, m_touch_px * 255 / 639);
			queue_packet(0x08, m_touch_screen * 2 + 1, m_touch_py * 255 / 479);
		}
		else if (m_touch_down)
		{
			queue_packet(0x08, m_touch_screen * 2 + 0, 0xff);
			queue_packet(0x08, m_touch_screen * 2 + 1, 0xff);
		}
		m_touch_down = down;
	}
}

static void link_devices(device_slot_interface &device)
{
	device.option_add("panel", HOG2_PANEL);
	device.option_add("null_modem", NULL_MODEM);
}

namespace {

// ------------------------------------------------------------------ driver

class wholehog2_state : public driver_device
{
public:
	wholehog2_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_uart(*this, "uart%u", 0U)
		, m_sio(*this, "sio")
		, m_kbd(*this, "at_keyboard")
		, m_vga(*this, "vga%u", 1U)
		, m_lcd(*this, "lcd%u", 1U)
		, m_conf(*this, "CONF")
		, m_panel(*this, "link:panel")
		, m_disks(*this, "disks")
		, m_flash(*this, "flash")
		, m_lowram(*this, "lowram")
		, m_bootrom(*this, "bootrom")
	{ }

	void wholehog2(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	// synthetic boot ROM layout (region F)
	static constexpr uint32_t ROM_BASE   = 0xf0000000;
	static constexpr uint32_t ROM_CODE   = ROM_BASE + 0x0000;   // reset code
	static constexpr uint32_t ROM_STUBS  = ROM_BASE + 0x0100;   // 32 system call stubs, 16 bytes each
	static constexpr uint32_t ROM_SAT    = ROM_BASE + 0x1000;
	static constexpr uint32_t ROM_PRCB   = ROM_BASE + 0x1100;
	static constexpr uint32_t ROM_SPT    = ROM_BASE + 0x1200;   // system procedure table
	static constexpr uint32_t ROM_ITAB   = ROM_BASE + 0x2000;   // interrupt table (0x404 bytes)
	static constexpr uint32_t ROM_TRAP   = ROM_BASE + 0xf000;   // trap registers, one per system call
	static constexpr uint32_t INT_STACK  = 0x00008000;          // interrupt stack in region 0 RAM
	static constexpr uint32_t OS_STACK   = 0x20000000;          // OS stack in fast SRAM (the OS walks frames in 20000000-2001ffff)
	static constexpr int TICK_HZ = 1000;

	void main_map(address_map &map) ATTR_COLD;

	uint16_t pld_r(offs_t offset);
	void pld_w(offs_t offset, uint16_t data);
	uint8_t sio_r(offs_t offset);
	uint8_t m_last_msr = 0xff;
	void sio_w(offs_t offset, uint8_t data);
	template <unsigned N> uint16_t fifo_r(offs_t offset);
	template <unsigned N> void fifo_w(offs_t offset, uint16_t data);
	uint8_t ltc_r(offs_t offset);
	void ltc_w(offs_t offset, uint8_t data);
	void trap_w(offs_t offset, uint32_t data);

	template <unsigned N> void uart_irq_w(int state);
	void sio_irq4_w(int state) { set_irq(0x22, state); }
	void sio_irq3_w(int state) { set_irq(0x32, state); }
	void kbd_irq_w(int state) { if (state) { set_irq(0x12, 0); set_irq(0x12, 1); } }   // one edge per queued scancode
	void set_irq(int vector, int state);
	void refresh_irqs();
	TIMER_CALLBACK_MEMBER(tick);

	void dmx_word(int port, uint16_t word);
	void dmx_frame_done(int port);

	void build_bootrom();
	uint32_t reg(int n) { return m_maincpu->state_int(I960_G0 + n); }
	void set_result(uint32_t v) { m_maincpu->set_state_int(I960_G0, v); }

	required_device<i960_cpu_device> m_maincpu;
	required_device_array<ns16550_device, 6> m_uart;
	required_device<fdc37c665gt_device> m_sio;
	required_device<at_keyboard_device> m_kbd;
	required_device_array<cirrus_gd5428_vga_device, 2> m_vga;
	required_device_array<f65535_vga_device, 2> m_lcd;
	required_ioport m_conf;
	optional_device<hog2_panel_device> m_panel;
	template <unsigned N> uint32_t lcd_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	template <unsigned N> uint32_t lcd_r(offs_t offset, uint32_t mem_mask);
	template <unsigned N> void lcd_w(offs_t offset, uint32_t data, uint32_t mem_mask);
	uint32_t m_lcd_stats[10] = {};
	void lcd2_seq_w(offs_t offset, uint8_t data);
	void lcd2_gc_w(offs_t offset, uint8_t data);
	uint8_t m_seq_idx = 0, m_gc_idx = 0, m_seq_regs[32] = {}, m_gc_regs[16] = {};
	int m_trace_left = 0;
	void dump_lcd_ram();
	required_region_ptr<uint8_t> m_disks;
	required_shared_ptr<uint32_t> m_flash;
	required_shared_ptr<uint32_t> m_lowram;
	required_shared_ptr<uint32_t> m_bootrom;

	emu_timer *m_tick_timer = nullptr;
	artnet_sender m_artnet;

	uint16_t m_pld_ctrl = 0;
	uint8_t m_ltc_regs[16] = {};
	uint8_t m_irq_state[256] = {};
	uint32_t m_isr[256] = {};
	bool m_int_enabled = false;
	uint8_t m_pin_enabled = 0;
	uint32_t m_ticks = 0;
	int m_irq_log = 0;

	// DMX output decode, one state per FIFO
	int m_dmx_pos[4] = { -1, -1, -1, -1 };
	uint8_t m_dmx_buf[4][513] = {};
	uint8_t m_dmx_last[4][512] = {};
	uint32_t m_dmx_frames[4] = {};
	attotime m_artnet_last[4];
};


// ------------------------------------------------------------------ memory map (MEMMAP2, rev MK2/3)

static void hog2_floppies(device_slot_interface &device)
{
	device.option_add("35hd", FLOPPY_35_HD);
}

void wholehog2_state::main_map(address_map &map)
{
	// region 0: RAM. Holds the i960 boot record (read from 0 by the core), the OS's interrupt scratch area (C0-100)
	// and the interrupt stack.
	map(0x00000000, 0x0000ffff).flags(i960_cpu_device::BURST).ram().share("lowram");

	// region 1: PLD registers at A3..A2  (01 read keyboard, 10 read port TXRDY/RXRDY, 11 write control)
	map(0x10000000, 0x1000000f).rw(FUNC(wholehog2_state::pld_r), FUNC(wholehog2_state::pld_w));

	// region 2: 128 KB fast SRAM (battery backed)
	map(0x20000000, 0x2001ffff).flags(i960_cpu_device::BURST).ram().share("nvram");

	// region 3: FDC37C652 super I/O with IBM AT port numbering (3F0 floppy, 378 printer, 3F8 serial, 2F8 link)
	map(0x30000000, 0x30000fff).rw(FUNC(wholehog2_state::sio_r), FUNC(wholehog2_state::sio_w));

	// region 4: 4 MB main PSRAM (battery backed)
	map(0x40000000, 0x403fffff).flags(i960_cpu_device::BURST).ram();

	// region 5: 2 MB flash: OS image + ROM Library, filled from the release floppies in machine_start
	map(0x50000000, 0x501fffff).flags(i960_cpu_device::BURST).rom().share("flash");   // N28F020s: plain stores have no effect (command sequence not emulated)

	// regions 8/9: Cirrus GD5426 VGA 1 (zone 7) and VGA 2 (zone F), IBM AT layout: I/O 3B0-3DF, memory A0000-BFFFF
	map(0x87000100, 0x87000103).nopw();   // VGA POS / setup registers (102, 46E8)
	map(0x870046e8, 0x870046eb).nopw();
	map(0x8f000100, 0x8f000103).nopw();
	map(0x8f0046e8, 0x8f0046eb).nopw();
	map(0x870003b0, 0x870003df).m(m_vga[0], FUNC(cirrus_gd5428_vga_device::io_map));
	map(0x8f0003b0, 0x8f0003df).m(m_vga[1], FUNC(cirrus_gd5428_vga_device::io_map));
	map(0x97000000, 0x970fffff).flags(i960_cpu_device::BURST).rw(m_vga[0], FUNC(cirrus_gd5428_vga_device::mem_linear_r), FUNC(cirrus_gd5428_vga_device::mem_linear_w));
	map(0x9f000000, 0x9f0fffff).flags(i960_cpu_device::BURST).rw(m_vga[1], FUNC(cirrus_gd5428_vga_device::mem_linear_r), FUNC(cirrus_gd5428_vga_device::mem_linear_w));

	// regions A/B: C&T 65510 LCD controllers (VGA compatible), LCD 1 (zone 7) and LCD 2 (zone F)
	map(0xa70003b0, 0xa70003df).m(m_lcd[0], FUNC(f65535_vga_device::io_map));
	map(0xaf0003b0, 0xaf0003df).m(m_lcd[1], FUNC(f65535_vga_device::io_map));
	map(0xaf0013b0, 0xaf0013df).m(m_lcd[1], FUNC(f65535_vga_device::io_map));   // mirror used by the register tracer
	map(0xaf0003c4, 0xaf0003c5).w(FUNC(wholehog2_state::lcd2_seq_w));
	map(0xaf0003ce, 0xaf0003cf).w(FUNC(wholehog2_state::lcd2_gc_w));
	// LCD frame buffers: the OS uses the 64 KB at linear offset 0x20000 of each controller in VGA 16-colour planar
	// mode (sequencer memory mode 06), writing planes through the map mask.  Route the window through the VGA
	// device's planar write logic; lcd_update decodes the four planes into 16 grey levels.
	map(0xb7020000, 0xb702ffff).flags(i960_cpu_device::BURST).rw(FUNC(wholehog2_state::lcd_r<0>), FUNC(wholehog2_state::lcd_w<0>));
	map(0xbf020000, 0xbf02ffff).flags(i960_cpu_device::BURST).rw(FUNC(wholehog2_state::lcd_r<1>), FUNC(wholehog2_state::lcd_w<1>));

	// region C: DAK010 LTC reader
	map(0xc0000000, 0xc000003f).rw(FUNC(wholehog2_state::ltc_r), FUNC(wholehog2_state::ltc_w));

	// region D: 16C550s, 8 consecutive byte registers (byte enables select the lane, A2 the upper half)
	map(0xd5000000, 0xd5000007).rw(m_uart[0], FUNC(ns16550_device::ins8250_r), FUNC(ns16550_device::ins8250_w));   // P0 MIDI
	map(0xd7000000, 0xd7000007).rw(m_uart[1], FUNC(ns16550_device::ins8250_r), FUNC(ns16550_device::ins8250_w));   // P1 DMX in
	map(0xd9000000, 0xd9000007).rw(m_uart[2], FUNC(ns16550_device::ins8250_r), FUNC(ns16550_device::ins8250_w));   // P2
	map(0xdb000000, 0xdb000007).rw(m_uart[3], FUNC(ns16550_device::ins8250_r), FUNC(ns16550_device::ins8250_w));   // P3
	map(0xdd000000, 0xdd000007).rw(m_uart[4], FUNC(ns16550_device::ins8250_r), FUNC(ns16550_device::ins8250_w));   // P4
	map(0xdf000000, 0xdf000007).rw(m_uart[5], FUNC(ns16550_device::ins8250_r), FUNC(ns16550_device::ins8250_w));   // P5 expansion

	// region E: IDT72125 DMX transmit FIFOs (16-bit words, bit-serial frames)
	map(0xe7000000, 0xe7000003).rw(FUNC(wholehog2_state::fifo_r<0>), FUNC(wholehog2_state::fifo_w<0>));
	map(0xe9000000, 0xe9000003).rw(FUNC(wholehog2_state::fifo_r<1>), FUNC(wholehog2_state::fifo_w<1>));
	map(0xeb000000, 0xeb000003).rw(FUNC(wholehog2_state::fifo_r<2>), FUNC(wholehog2_state::fifo_w<2>));
	map(0xed000000, 0xed000003).rw(FUNC(wholehog2_state::fifo_r<3>), FUNC(wholehog2_state::fifo_w<3>));

	// region F: synthetic boot ROM (RAM backed, built at reset) and the system call trap registers
	map(0xf0000000, 0xf000efff).flags(i960_cpu_device::BURST).ram().share("bootrom");
	map(0xf000f000, 0xf000f0ff).w(FUNC(wholehog2_state::trap_w));
}

// ------------------------------------------------------------------ synthetic boot ROM

void wholehog2_state::build_bootrom()
{
	uint32_t *rom = m_bootrom.target();
	std::fill_n(rom, 0xf000 / 4, 0);
	auto put = [rom](uint32_t addr, uint32_t v) { rom[(addr - ROM_BASE) / 4] = v; };

	// i960 encodings used below
	auto memb_abs = [](uint8_t op, int r) { return (uint32_t(op) << 24) | (uint32_t(r) << 19) | 0x3000; };   // op reg, disp32
	constexpr uint32_t RET = 0x0a000000;
	constexpr uint32_t B_SELF = 0x08000000;
	auto mov_lit = [](int lit, int dst) { return 0x5c000000 | (uint32_t(dst) << 19) | 0x800 | (0xc << 7) | uint32_t(lit); };

	// reset code: fp/sp into fast SRAM, clear pfp, zero the three boot arguments, call the OS entry from flash+4
	uint32_t a = ROM_CODE;
	put(a, memb_abs(0x8c, 31)); put(a + 4, OS_STACK); a += 8;            // lda OS_STACK, fp
	put(a, memb_abs(0x8c, 1));  put(a + 4, OS_STACK + 64); a += 8;       // lda OS_STACK+64, sp
	put(a, mov_lit(0, 0)); a += 4;                                       // mov 0, pfp
	put(a, mov_lit(0, 16)); a += 4;                                      // mov 0, g0
	put(a, mov_lit(0, 17)); a += 4;                                      // mov 0, g1
	put(a, mov_lit(0, 18)); a += 4;                                      // mov 0, g2
	put(a, memb_abs(0x8c, 20)); put(a + 4, 0x001f0000); a += 8;          // lda 001f0000, g4 (process priority mask)
	put(a, mov_lit(0, 21)); a += 4;                                      // mov 0, g5
	put(a, 0x65000000 | (21 << 19) | (20 << 14) | (5 << 7) | 20); a += 4; // modpc g4, g4, g5: priority 0, interrupts deliverable
	put(a, memb_abs(0x90, 29)); put(a + 4, 0x50000008); a += 8;          // ld 50000008, g13 (OS entry pointer)
	put(a, 0x86000000 | (29 << 14) | (0x8 << 10)); a += 4;               // callx (g13)
	const uint32_t idle = a;
	put(a, B_SELF); a += 4;                                              // b .
	const uint32_t iret = a;
	put(a, 0x5c082603); a += 4;                                          // mov r3, sf1: restore IMSK saved on interrupt entry
	put(a, RET); a += 4;                                                 // ret: default entry for uninstalled interrupt vectors

	// system call stubs: st g0, TRAP+N*4 ; ret
	for (int n = 0; n < 32; n++)
	{
		const uint32_t s = ROM_STUBS + n * 16;
		put(s, memb_abs(0x92, 16)); put(s + 4, ROM_TRAP + n * 4);
		put(s + 8, RET);
	}

	// system address table: entry 152 = system procedure table
	put(ROM_SAT + 152, ROM_SPT);
	put(ROM_SAT + 156, 0x304000fb);
	// system procedure table: supervisor stack pointer, then entries (type 00 = local procedure) at +48
	put(ROM_SPT + 12, INT_STACK);
	for (int n = 0; n < 32; n++)
		put(ROM_SPT + 48 + n * 4, ROM_STUBS + n * 16);
	// PRCB: interrupt table and interrupt stack
	put(ROM_PRCB + 16, ROM_ITAB);
	put(ROM_PRCB + 20, ROM_ITAB);
	put(ROM_PRCB + 24, INT_STACK);
	// interrupt table: pending words cleared, all vectors to a "ret"
	for (int v = 8; v < 256; v++)
		put(ROM_ITAB + 36 + (v - 8) * 4, iret);

	// boot record read by the core from address 0
	m_lowram[0] = ROM_SAT;
	m_lowram[1] = ROM_PRCB;
	m_lowram[2] = 0;
	m_lowram[3] = ROM_CODE;
}

void wholehog2_state::trap_w(offs_t offset, uint32_t data)
{
	const int n = offset & 31;
	const uint32_t g0 = reg(0), g1 = reg(1), g2 = reg(2), g3 = reg(3);
	const uint32_t rip = m_maincpu->state_int(I960_R0 + 2);   // return address into the OS
	uint32_t result = 0;

	switch (n)
	{
	case 3:     // install interrupt handler (vector, handler + 4, 0)
	{
		const int vector = g0 & 0xff;
		m_isr[vector] = g1;
		if (vector >= 8)
			m_bootrom[(ROM_ITAB - ROM_BASE + 36 + (vector - 8) * 4) / 4] = g1;
		LOGMASKED(LOG_SYSCALL, "boot: install ISR vector %02x -> %08x (%08x)\n", vector, g1 - 4, g2);
		break;
	}
	case 4:     // query interrupt (vector) -> value
		LOGMASKED(LOG_SYSCALL, "boot: query interrupt %02x\n", g0);
		break;
	case 7:     // enable interrupts
	case 11:
		if (!m_int_enabled) LOGMASKED(LOG_SYSCALL, "boot: interrupts on (call %d) from %08x\n", n, rip);
		m_int_enabled = true;
		refresh_irqs();
		break;
	case 8:     // disable interrupts
		if (m_int_enabled) LOGMASKED(LOG_SYSCALL, "boot: interrupts off from %08x\n", rip);
		m_int_enabled = false;
		refresh_irqs();
		break;
	case 9:     // enable XINT pin
		m_pin_enabled |= 1 << (g0 & 7);
		LOGMASKED(LOG_SYSCALL, "boot: enable pin %d\n", g0 & 7);
		if ((g0 & 7) == 7)
			m_tick_timer->adjust(attotime::from_hz(TICK_HZ), 0, attotime::from_hz(TICK_HZ));
		break;
	case 10:    // disable XINT pin
		m_pin_enabled &= ~(1 << (g0 & 7));
		LOGMASKED(LOG_SYSCALL, "boot: disable pin %d\n", g0 & 7);
		if ((g0 & 7) == 7)
			m_tick_timer->adjust(attotime::never);
		break;
	case 23:    // touch screen calibration for unit g0: *g1 = {x origin, y origin}, *g2 = {x span, y span}
	{
		address_space &sp = m_maincpu->space(AS_PROGRAM);
		sp.write_dword(g1, 0); sp.write_dword(g1 + 4, 0);        // origin: screen = (raw - origin) * size / span
		sp.write_dword(g2, 255); sp.write_dword(g2 + 4, 255);
		LOGMASKED(LOG_SYSCALL, "boot: touch calibration read, unit %d\n", g0);
		break;
	}
	case 24:    // store touch screen calibration (accepted, not persisted)
		LOGMASKED(LOG_SYSCALL, "boot: touch calibration write, unit %d\n", g0);
		break;
	case 16:    // exit / fatal
		logerror("boot: OS exit(%d) at %08x\n", g0, rip);
		break;
	case 19:    // configuration flag query
		LOGMASKED(LOG_SYSCALL, "boot: query flag %d\n", g0);
		result = 0;
		break;
	default:
		LOGMASKED(LOG_SYSCALL, "boot: syscall %d (%08x, %08x, %08x, %08x) at %08x\n", n, g0, g1, g2, g3, rip);
		break;
	}
	set_result(result);
}

// ------------------------------------------------------------------ interrupts

void wholehog2_state::set_irq(int vector, int state)
{
	m_irq_state[vector & 0xff] = state ? 1 : 0;
	if (m_irq_log < 40 && vector != 0xd2)
	{
		m_irq_log++;
		LOGMASKED(LOG_SYSCALL, "irq vector %02x %s (handler %08x, global %d)\n", vector, state ? "asserted" : "cleared", m_isr[vector & 0xff], m_int_enabled);
	}
	if (m_int_enabled)
		m_maincpu->set_vector_irq(vector, state);
}

void wholehog2_state::refresh_irqs()
{
	for (int v = 8; v < 256; v++)
		if (m_irq_state[v])
			m_maincpu->set_vector_irq(v, m_int_enabled ? 1 : 0);
}

template <unsigned N> void wholehog2_state::uart_irq_w(int state)
{
	static constexpr int vectors[6] = { 0x72, 0xc2, 0, 0, 0, 0x62 };
	if (vectors[N])
		set_irq(vectors[N], state);
}

TIMER_CALLBACK_MEMBER(wholehog2_state::tick)
{
	m_ticks++;
	if ((m_ticks % (TICK_HZ * 30)) == 0)
	{
		LOG("tick %u: DMX frames %u %u %u %u\n", m_ticks, m_dmx_frames[0], m_dmx_frames[1], m_dmx_frames[2], m_dmx_frames[3]);
		for (int i = 0; i < 4; i++)
			logerror("lcd access widths: reads 1:%u 2:%u 3:%u 4:%u  writes 1:%u 2:%u 3:%u 4:%u\n", m_lcd_stats[1], m_lcd_stats[2], m_lcd_stats[3], m_lcd_stats[4], m_lcd_stats[5], m_lcd_stats[6], m_lcd_stats[7], m_lcd_stats[8]);
	}
	if (m_int_enabled && m_isr[0xd2])
	{
		m_maincpu->set_vector_irq(0xd2, 1);
		m_maincpu->set_vector_irq(0xd2, 0);
	}
}

// ------------------------------------------------------------------ glue

uint16_t wholehog2_state::pld_r(offs_t offset)
{
	switch ((offset >> 1) & 3)   // 16-bit handler: offset is in halfwords, the PLD decodes A3..A2
	{
	case 1:
	{
		// AT keyboard through the PLD shift register. From the OS's scancode table (0x501489e0): the 9-bit value is
		// the 11-bit frame shifted in as it arrives: d0..d6 of the set-2 scancode land in bits 8..2, d7 in bit 1 and the
		// odd parity bit in bit 0 (verified against the table: E0 -> 00E extended, F0 -> 01F release, 5A Enter -> 0B5).
		if (machine().side_effects_disabled())
			return 0xffff;
		const uint8_t sc = m_kbd->read();
		uint16_t code = 0;
		for (int b = 0; b < 7; b++)
			code |= BIT(sc, b) << (8 - b);
		code |= BIT(sc, 7) << 1;
		code += (std::popcount(unsigned(sc)) & 1) ? 0 : 1;
		return code & 0x1ff;
	}
	case 2: return 0x00ff;                  // TXRDY/RXRDY bits of the ports
	default: return m_pld_ctrl;
	}
}

void wholehog2_state::pld_w(offs_t offset, uint16_t data)
{
	if (((offset >> 1) & 3) == 3) m_pld_ctrl = data;
	LOG("PLD write %d = %04x\n", offset & 3, data);
}

uint8_t wholehog2_state::sio_r(offs_t offset)
{
	const uint8_t data = m_sio->read(offset);
	if ((offset & 0xff8) == 0x3f0 && !machine().side_effects_disabled() && (offset != 0x3f4 || data != m_last_msr))
	{
		if (offset == 0x3f4) m_last_msr = data;
		LOGMASKED(LOG_FDC, "FDC r %03x = %02x pc=%08x\n", offset, data, m_maincpu->pc());
	}
	if (offset == 0x2f8 && !machine().side_effects_disabled() && !(m_sio->read(0x2fb) & 0x80))
		LOGMASKED(LOG_LINK, "LINK rx %02x\n", data);
	return data;
}

void wholehog2_state::sio_w(offs_t offset, uint8_t data)
{
	if (((offset & 0xff8) == 0x2f8 || (offset & 0xff8) == 0x3f8) && ((offset & 7) != 0 || (m_sio->read((offset & 0xff8) | 3) & 0x80)))
		LOGMASKED(LOG_LINK, "SIO reg %03x = %02x\n", offset, data);
	if (offset == 0x3f8 && !(m_sio->read(0x3fb) & 0x80))
		LOGMASKED(LOG_LINK, "SERIAL tx %02x\n", data);
	if (offset == 0x2f8 && !(m_sio->read(0x2fb) & 0x80))
		LOGMASKED(LOG_LINK, "LINK tx %02x\n", data);
	if ((offset & 0xff8) == 0x3f0)
		LOGMASKED(LOG_FDC, "FDC w %03x = %02x pc=%08x\n", offset, data, m_maincpu->pc());
	m_sio->write(offset, data);
}

template <unsigned N> uint16_t wholehog2_state::fifo_r(offs_t offset) { return 0; }
template <unsigned N> void wholehog2_state::fifo_w(offs_t offset, uint16_t data) { dmx_word(N, data); }

// ------------------------------------------------------------------ LCD frame buffers

// Frame buffer layout found from a RAM dump: 640x480, 1 bpp, 128 bytes per line (1024 pixel pitch), starting at
// video memory offset 0x20000, bit set = light (the panels are monochrome STN, drawn light-on-dark by the OS's palette)
template <unsigned N> uint32_t wholehog2_state::lcd_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	// 640x480, 128 bytes per plane row, 4 planes = 16 grey levels.  Plane bits set drive the pixel dark on the
	// STN panel (the handbook shows light buttons with dark text); the polarity is configurable.
	const bool invert = BIT(m_conf->read(), 0);
	uint32_t shades[16];
	for (int v = 0; v < 16; v++)
	{
		// the OS uses indexes 0-5 for backgrounds and bevels and 15 for text: spread those for contrast
		const int lv = invert ? 15 - v : v;
		const int level = std::max(0, 15 - lv * 2);           // 0 = dark ... 15 = light
		const int r = 0x18 + (0xf0 - 0x18) * level / 15, g = 0x1c + (0xf2 - 0x1c) * level / 15, b = 0x24 + (0xee - 0x24) * level / 15;
		shades[v] = 0xff000000 | (r << 16) | (g << 8) | b;
	}
	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
	{
		uint32_t *dst = &bitmap.pix(y, 0);
		for (int bx = 0; bx < 80; bx++)
		{
			const offs_t off = y * 128 + bx;
			const uint8_t p0 = m_lcd[N]->mem_linear_r(off), p1 = m_lcd[N]->mem_linear_r(off + 0x10000),
					p2 = m_lcd[N]->mem_linear_r(off + 0x20000), p3 = m_lcd[N]->mem_linear_r(off + 0x30000);
			for (int bit = 0; bit < 8; bit++)
			{
				const int sh = 7 - bit;
				const int v = BIT(p0, sh) | (BIT(p1, sh) << 1) | (BIT(p2, sh) << 2) | (BIT(p3, sh) << 3);
				dst[bx * 8 + bit] = shades[v];
			}
		}
	}
	// the touch pointer: a cross on the LCD it is over, filled while the finger is down
	int px, py;
	bool down;
	if (m_panel && m_panel->pointer(N, px, py, down))
	{
		const uint32_t col = down ? 0xffff4040 : 0xff2060ff;
		for (int d = -7; d <= 7; d++)
		{
			if (px + d >= 0 && px + d < 640 && py >= cliprect.top() && py <= cliprect.bottom()) bitmap.pix(py, px + d) = col;
			if (py + d >= cliprect.top() && py + d <= cliprect.bottom() && px >= 0 && px < 640) bitmap.pix(py + d, px) = col;
		}
	}
	return 0;
}

void wholehog2_state::dump_lcd_ram()
{
	for (int n = 0; n < 2; n++)
	{
		FILE *f = fopen(n ? "lcd2.bin" : "lcd1.bin", "wb");
		if (!f) continue;
		for (offs_t a = 0; a < 0x40000; a++) fputc(m_lcd[n]->mem_linear_r(a), f);
		fclose(f);
	}
}

void wholehog2_state::lcd2_seq_w(offs_t offset, uint8_t data)
{
	if (offset == 0) m_seq_idx = data & 31; else m_seq_regs[m_seq_idx] = data;
	m_maincpu->space(AS_PROGRAM).write_byte(0xaf0013c4 + offset, data);
}

void wholehog2_state::lcd2_gc_w(offs_t offset, uint8_t data)
{
	if (offset == 0) m_gc_idx = data & 15; else m_gc_regs[m_gc_idx] = data;
	m_maincpu->space(AS_PROGRAM).write_byte(0xaf0013ce + offset, data);
}

// LCD planar window: byte lanes go to the VGA device one at a time (statistics on access widths for now)
template <unsigned N> uint32_t wholehog2_state::lcd_r(offs_t offset, uint32_t mem_mask)
{
	uint32_t v = 0;
	int lanes = 0;
	for (int i = 0; i < 4; i++)
		if (mem_mask & (0xffU << (i * 8))) { v |= uint32_t(m_lcd[N]->mem_r(offset * 4 + i)) << (i * 8); lanes++; }
	if (!machine().side_effects_disabled()) m_lcd_stats[lanes]++;
	return v;
}

template <unsigned N> void wholehog2_state::lcd_w(offs_t offset, uint32_t data, uint32_t mem_mask)
{
	int lanes = 0;
	if (N == 1 && m_trace_left > 0)
	{
		const int row = (offset * 4) / 128, col = (offset * 4) % 128;
		if (row == 200 && col >= 32 && col < 68)
		{
			m_trace_left--;
			logerror("lcd2 write row %d col %d data %08x mask %08x | seq map %02x mode %02x | gc sr %02x esr %02x rot %02x rmap %02x mode %02x bitmask %02x | t=%s pc=%08x\n",
					row, col, data, mem_mask, m_seq_regs[2], m_seq_regs[4], m_gc_regs[0], m_gc_regs[1], m_gc_regs[3], m_gc_regs[4], m_gc_regs[5], m_gc_regs[8],
					machine().time().as_string(3), m_maincpu->pc());
		}
	}
	for (int i = 0; i < 4; i++)
		if (mem_mask & (0xffU << (i * 8)))
		{


			m_lcd[N]->mem_w(offset * 4 + i, (data >> (i * 8)) & 0xff);
			lanes++;
		}
	m_lcd_stats[4 + lanes]++;
}

uint8_t wholehog2_state::ltc_r(offs_t offset) { return m_ltc_regs[offset & 15]; }
void wholehog2_state::ltc_w(offs_t offset, uint8_t data) { m_ltc_regs[offset & 15] = data; }

// ------------------------------------------------------------------ DMX -> Art-Net

void wholehog2_state::dmx_word(int port, uint16_t word)
{
	// 0000 = 16 space bits (break), C000 = 14 space bits + 2 marks (end of break / mark after break),
	// xxxx xxxx 0111 with 1111/1110 above = start bit, 8 data bits, stop bits: a serial character
	if (word == 0x0000)
	{
		if (m_dmx_pos[port] > 0)
			dmx_frame_done(port);
		m_dmx_pos[port] = 0;
	}
	else if (word == 0xc000)
	{
		if (m_dmx_pos[port] < 0)
			m_dmx_pos[port] = 0;
	}
	else if (word == 0xffff)
	{
		// end of frame marker written by the OS after the last slot
		if (m_dmx_pos[port] > 0)
			dmx_frame_done(port);
		m_dmx_pos[port] = -1;
	}
	else if ((word & 0x000f) == 0x0007)
	{
		if (m_dmx_pos[port] >= 0 && m_dmx_pos[port] < 513)
			m_dmx_buf[port][m_dmx_pos[port]++] = (word >> 4) & 0xff;
		if (m_dmx_pos[port] == 513)
			dmx_frame_done(port);
	}
	else
		LOGMASKED(LOG_DMX, "DMX%d: unknown FIFO word %04x\n", port + 1, word);
}

void wholehog2_state::dmx_frame_done(int port)
{
	const int slots = m_dmx_pos[port] - 1;
	m_dmx_pos[port] = -1;
	if (slots < 1)
		return;
	m_dmx_frames[port]++;
	if (m_dmx_buf[port][0] == 0)        // null start code: dimmer data
	{
		// the OS refreshes its FIFOs far faster than a 250 kbit/s DMX line could carry (the PLD paces the real
		// hardware), so send Art-Net at most 40 times a second per universe, immediately on a change and at
		// least once a second otherwise
		const bool changed = std::memcmp(m_dmx_last[port], &m_dmx_buf[port][1], slots) != 0;
		const attotime now = machine().time();
		const attotime since = now - m_artnet_last[port];
		if ((changed && since >= attotime::from_msec(25)) || since >= attotime::from_seconds(1))
		{
			std::memcpy(m_dmx_last[port], &m_dmx_buf[port][1], slots);
			m_artnet.send_dmx(port, m_dmx_last[port], slots);
			m_artnet_last[port] = now;
		}
	}
	if ((m_dmx_frames[port] % 200) == 1)
		LOGMASKED(LOG_DMX, "DMX%d: frame %u, start code %02x, %d slots, ch1-4 = %02x %02x %02x %02x\n", port + 1, m_dmx_frames[port],
				m_dmx_buf[port][0], slots, m_dmx_buf[port][1], m_dmx_buf[port][2], m_dmx_buf[port][3], m_dmx_buf[port][4]);
}

// ------------------------------------------------------------------ machine

void wholehog2_state::machine_start()
{
	// flash = disk 1 payload at 0 + disk 2 payload at 0x15E000 (one contiguous OS image); 12-byte headers and 4-byte trailers dropped
	uint8_t *flash = reinterpret_cast<uint8_t *>(m_flash.target());
	memset(flash, 0xff, 0x200000);
	memcpy(flash, &m_disks[0x0c], 0x15e000);
	memcpy(flash + 0x15e000, &m_disks[0x15e010 + 0x0c], 0xa1f00);

	m_tick_timer = timer_alloc(FUNC(wholehog2_state::tick), this);
	// the CPU reads the boot record at its own reset, which precedes machine_reset: build the ROM image up front
	build_bootrom();
	m_artnet.open();
	if (m_artnet.ok())
		logerror("Art-Net: sending DMX ports 1-4 as universes %d-%d\n", m_artnet.universe(), m_artnet.universe() + 3);
	else
		logerror("Art-Net: no destination (set HOG2_ARTNET_HOST)\n");

	save_item(NAME(m_pld_ctrl));
	save_item(NAME(m_ltc_regs));
	save_item(NAME(m_irq_state));
	save_item(NAME(m_isr));
	save_item(NAME(m_int_enabled));
	save_item(NAME(m_pin_enabled));
	save_item(NAME(m_ticks));
	save_item(NAME(m_dmx_pos));
	save_item(NAME(m_dmx_buf));
	save_item(NAME(m_dmx_last));
	save_item(NAME(m_dmx_frames));
}

void wholehog2_state::machine_reset()
{
	std::fill_n(m_irq_state, 256, 0);
	std::fill_n(m_isr, 256, 0);
	m_int_enabled = true;      // the boot ROM hands over with interrupts enabled; calls 8/7 bracket critical sections
	m_pin_enabled = 0;
	m_tick_timer->adjust(attotime::never);
	for (int i = 0; i < 4; i++) { m_dmx_pos[i] = -1; m_artnet_last[i] = attotime::zero; }
	// The OS polls each DMX port UART's modem status: DSR (bit 5) makes it create the port's DMX output object and
	// CTS (bit 4) keeps the output refreshing (on the real board these are sense lines from the output driver).
	for (int i = 1; i <= 4; i++)
	{
		m_uart[i]->cts_w(0);
		m_uart[i]->dsr_w(0);
	}
	build_bootrom();
	logerror("wholehog2: OS entry %08x\n", m_flash[2]);
}

void wholehog2_state::wholehog2(machine_config &config)
{
	I80960KB(config, m_maincpu, XTAL(50'000'000));
	m_maincpu->set_addrmap(AS_PROGRAM, &wholehog2_state::main_map);

	NVRAM(config, "nvram", nvram_device::DEFAULT_ALL_0);

	for (int i = 0; i < 6; i++)
		NS16550(config, m_uart[i], XTAL(8'000'000));   // 8 MHz: divisor 2 = 250 kbaud DMX, 16 = 31250 MIDI
	m_uart[0]->out_int_callback().set(FUNC(wholehog2_state::uart_irq_w<0>));
	m_uart[1]->out_int_callback().set(FUNC(wholehog2_state::uart_irq_w<1>));
	m_uart[2]->out_int_callback().set(FUNC(wholehog2_state::uart_irq_w<2>));
	m_uart[3]->out_int_callback().set(FUNC(wholehog2_state::uart_irq_w<3>));
	m_uart[4]->out_int_callback().set(FUNC(wholehog2_state::uart_irq_w<4>));
	m_uart[5]->out_int_callback().set(FUNC(wholehog2_state::uart_irq_w<5>));

	// AT keyboard on the PLD's KCLK/KDATA shift register (set 2 scancodes), interrupt vector 12
	AT_KEYB(config, m_kbd, pc_keyboard_device::KEYBOARD_TYPE::AT, 2);
	m_kbd->keypress().set(FUNC(wholehog2_state::kbd_irq_w));

	FDC37C665GT(config, m_sio, XTAL(24'000'000));
	// 3.5" HD drive on the back panel; the OS drives the 82077 by polling (no DMA, no interrupt), DOS FAT12 disks
	FLOPPY_CONNECTOR(config, "sio:fdc:0", hog2_floppies, "35hd", floppy_image_device::default_pc_floppy_formats).enable_sound(true);
	m_sio->irq4().set(FUNC(wholehog2_state::sio_irq4_w));   // serial 3F8 -> vector 22
	m_sio->irq3().set(FUNC(wholehog2_state::sio_irq3_w));   // link 2F8 -> vector 32

	// the LINK port (2F8) talks to the front panel processor; the serial port (3F8) is the rear RS232
	rs232_port_device &link(RS232_PORT(config, "link", link_devices, "panel"));
	m_sio->txd2().set(link, FUNC(rs232_port_device::write_txd));
	link.rxd_handler().set(m_sio, FUNC(fdc37c665gt_device::rxd2_w));
	rs232_port_device &serial(RS232_PORT(config, "serial", default_rs232_devices, nullptr));
	m_sio->txd1().set(serial, FUNC(rs232_port_device::write_txd));
	serial.rxd_handler().set(m_sio, FUNC(fdc37c665gt_device::rxd1_w));

	// two built-in touch LCDs
	for (int i = 0; i < 2; i++)
	{
		screen_device &screen(SCREEN(config, i ? "lcdscreen2" : "lcdscreen1").set_lcd());
		screen.set_raw(XTAL(25'174'800), 900, 0, 640, 526, 0, 480);
		if (i)
			screen.set_screen_update(FUNC(wholehog2_state::lcd_update<1>));
		else
			screen.set_screen_update(FUNC(wholehog2_state::lcd_update<0>));
		F65535_VGA(config, m_lcd[i], 0);
		m_lcd[i]->set_screen(i ? "lcdscreen2" : "lcdscreen1");
		m_lcd[i]->set_vram_size(0x80000);
	}
	// two external VGA monitors
	for (int i = 0; i < 2; i++)
	{
		screen_device &screen(SCREEN(config, i ? "screen2" : "screen1"));
		screen.set_raw(XTAL(25'174'800), 900, 0, 640, 526, 0, 480);
		screen.set_screen_update(m_vga[i], FUNC(cirrus_gd5428_vga_device::screen_update));
		CIRRUS_GD5428_VGA(config, m_vga[i], 0);
		m_vga[i]->set_screen(i ? "screen2" : "screen1");
		m_vga[i]->set_vram_size(0x100000);
	}

	config.set_default_layout(layout_wholehog2);
}

// ------------------------------------------------------------------ software

static INPUT_PORTS_START( wholehog2 )
	PORT_START("CONF")
	PORT_CONFNAME(0x01, 0x00, "LCD polarity")
	PORT_CONFSETTING(0x00, "Set bit = dark pixel")
	PORT_CONFSETTING(0x01, "Set bit = light pixel")
INPUT_PORTS_END

ROM_START( wholehog2 )
	// the v3.3 b177 release floppies as distributed (each with a 16-byte header, dropped when the flash is filled)
	ROM_REGION( 0x200000, "disks", ROMREGION_ERASEFF )
	ROM_LOAD( "tnt_177_disk1_hog.bin", 0x000000, 0x15e010, CRC(12c8af70) SHA1(da056126c6bf112e81a92d4e1feb57ece4c0a68e) )
	ROM_LOAD( "tnt_177_disk2_hog.bin", 0x15e010, 0x0a1f10, CRC(3186a124) SHA1(4e900fbec0c1946b83311a88759d8caee91bbabb) )
ROM_END

} // anonymous namespace

//    YEAR  NAME       PARENT  COMPAT  MACHINE    INPUT  CLASS            INIT        COMPANY               FULLNAME                        FLAGS
COMP( 1997, wholehog2, 0,      0,      wholehog2, wholehog2, wholehog2_state, empty_init, "Flying Pig Systems", "Wholehog II (v3.3 build 177)", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
