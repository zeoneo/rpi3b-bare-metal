#ifndef _ELF_LOADER_
#define _ELF_LOADER_

#ifdef __cplusplus
extern "C"
{
#endif

#include<stdint.h>
#include<klib/elf.h>
#include<fs/romfs_new.h>

int load_elf(file_info_t *elf_file);
int elf_do_reloc(Elf32_Ehdr *hdr, Elf32_Rel *rel, Elf32_Shdr *reltab);
int elf_load_stage1(Elf32_Ehdr *hdr);
int elf_load_stage2(Elf32_Ehdr *hdr);
uint8_t elf_check_file(Elf32_Ehdr *hdr);
uint8_t elf_check_supported(Elf32_Ehdr *hdr);

#ifdef __cplusplus
}
#endif

#endif