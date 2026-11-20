/* vita-elf-create appends SCE import/export metadata to the executable segment.
 * Reserve an unmapped gap before read-only data, independent of code size.
 * A page boundary alone can leave less space than the metadata requires. */
SECTIONS
{
  . = ALIGN(0x1000) + 0x4000;
}
INSERT AFTER .fini;
