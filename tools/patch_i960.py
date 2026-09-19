"""Patch MAME's i960 core so a driver can post interrupts for arbitrary vectors (the Wholehog II boot ROM programs
the i960CF XINT pins with IMAP vectors such as 0x12, 0x22 ... 0xd2; MAME's KA/KB core only knows 4 lines with ICR vectors).
usage: patch_i960.py <mame source root>"""
import sys

root = sys.argv[1]
hp = root + "/src/devices/cpu/i960/i960.h"
cp = root + "/src/devices/cpu/i960/i960.cpp"

h = open(hp).read()
if "set_vector_irq" not in h:
    h = h.replace(
        "\tvirtual void execute_set_input(int inputnum, int state) override;",
        "\tvirtual void execute_set_input(int inputnum, int state) override;\n\n"
        "public:\n"
        "\t// post an interrupt for an arbitrary vector (Cx XINT pins with IMAP-programmed vectors, external controllers)\n"
        "\tvoid set_vector_irq(int vector, int state);\n"
        "protected:\n"
        "\tvoid post_irq(int vector, int irqline);", 1)
    h = h.replace("int8_t m_irq_line_state[4];", "int8_t m_irq_line_state[256];")
    open(hp, "w").write(h)

c = open(cp).read()
if "set_vector_irq" not in c:
    start = c.index("void i960_cpu_device::execute_set_input(int irqline, int state)")
    marker = "\tpriority = vector / 8;\n"
    end = c.index(marker, start) + len(marker)
    new = '''void i960_cpu_device::set_vector_irq(int vector, int state)
{
	vector &= 0xff;
	if (m_irq_line_state[vector] == state)
		return;
	m_irq_line_state[vector] = state;
	if (state)
		post_irq(vector, -1);
}

void i960_cpu_device::execute_set_input(int irqline, int state)
{
	if (m_irq_line_state[irqline] == state)
		return;

	m_irq_line_state[irqline] = state;

	int vector = 0;

	// We support the 4 external IRQ lines in "normal" mode only.
	// The i960's interrupt support is a bit more complete than that,
	// but Namco and Sega both went for the cheapest solution.

	switch (irqline)
	{
		case I960_IRQ0:
			vector = m_ICR & 0xff;
			break;

		case I960_IRQ1:
			vector = (m_ICR>>8)&0xff;
			break;

		case I960_IRQ2:
			vector = (m_ICR>>16)&0xff;
			break;

		case I960_IRQ3:
			vector = (m_ICR>>24)&0xff;
			break;
	}

	if(!vector)
	{
		logerror("i960: interrupt line %d in IAC mode, unsupported!\\n", irqline);
		return;
	}

	if (state)
		post_irq(vector, irqline);
}

void i960_cpu_device::post_irq(int vector, int irqline)
{
	int int_tab =  m_program.read_dword(m_PRCB+20);    // interrupt table
	int cpu_pri = (m_PC>>16)&0x1f;
	int priority;
	uint32_t pend, word, wordofs;
	const int state = 1;

	priority = vector / 8;
'''
    c = c[:start] + new + c[end:]
    c = c.replace("\t\t// and ack it to the core now that it's queued\n\t\tstandard_irq_callback(irqline, m_IP);",
                  "\t\t// and ack it to the core now that it's queued\n\t\tif (irqline >= 0)\n\t\t\tstandard_irq_callback(irqline, m_IP);")
    open(cp, "w").write(c)
print("patched")
