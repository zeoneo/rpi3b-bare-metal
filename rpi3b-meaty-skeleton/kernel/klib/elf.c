#include<stddef.h>
#include<stdlib.h>
#include<klib/elf.h>
#include<klib/elf_loader.h>
#include<klib/printk.h>


uint8_t elf_check_file(Elf32_Ehdr *hdr);
uint8_t elf_check_supported(Elf32_Ehdr *hdr);

#define ELF_RELOC_ERR -1
#define DO_386_32(S, A)	((S) + (A))
#define DO_386_PC32(S, A, P)	((S) + (A) - (P))

static inline Elf32_Shdr *elf_sheader(Elf32_Ehdr *hdr) {
	return (Elf32_Shdr *)((int)hdr + hdr->e_shoff);
}
 
static inline Elf32_Shdr *elf_section(Elf32_Ehdr *hdr, int idx) {
	return &elf_sheader(hdr)[idx];
}

static inline char *elf_str_table(Elf32_Ehdr *hdr) {
	if(hdr->e_shstrndx == SHN_UNDEF) return NULL;
	return (char *)hdr + elf_section(hdr, hdr->e_shstrndx)->sh_offset;
}
 
static inline char *elf_lookup_string(Elf32_Ehdr *hdr, int offset) {
	char *strtab = elf_str_table(hdr);
	if(strtab == NULL) return NULL;
	return strtab + offset;
}

static void *elf_lookup_symbol(const char *name) {
	printk(" %x", name);
	return NULL;
}

static int elf_get_symval(Elf32_Ehdr *hdr, int table, uint32_t idx) {
	if(table == SHN_UNDEF || idx == SHN_UNDEF) return 0;
	Elf32_Shdr *symtab = elf_section(hdr, table);
 
	uint32_t symtab_entries = symtab->sh_size / symtab->sh_entsize;
	if(idx >= symtab_entries) {
		printk("Symbol Index out of Range (%d:%u).\n", table, idx);
		return ELF_RELOC_ERR;
	}
 
	int symaddr = (int)hdr + symtab->sh_offset;
	Elf32_Sym *symbol = &((Elf32_Sym *)symaddr)[idx];

	if(symbol->st_shndx == SHN_UNDEF) {
		// External symbol, lookup value
		Elf32_Shdr *strtab = elf_section(hdr, symtab->sh_link);
		const char *name = (const char *)hdr + strtab->sh_offset + symbol->st_name;

		void *target = elf_lookup_symbol(name);
 
		if(target == NULL) {
			// Extern symbol not found
			if(ELF32_ST_BIND(symbol->st_info) & STB_WEAK) {
				// Weak symbol initialized as 0
				return 0;
			} else {
				printk("Undefined External Symbol : %s.\n", name);
				return ELF_RELOC_ERR;
			}
		} else {
			return (int)target;
		}
		} else if(symbol->st_shndx == SHN_ABS) {
		// Absolute symbol
		return symbol->st_value;
	} else {
		// Internally defined symbol
		Elf32_Shdr *target = elf_section(hdr, symbol->st_shndx);
		return (int)hdr + symbol->st_value + target->sh_offset;
	}
}

int elf_do_reloc(Elf32_Ehdr *hdr, Elf32_Rel *rel, Elf32_Shdr *reltab) {
	Elf32_Shdr *target = elf_section(hdr, reltab->sh_info);
 
	int addr = (int)hdr + target->sh_offset;
	int *ref = (int *)(addr + rel->r_offset);
    // Symbol value
	int symval = 0;
	if(ELF32_R_SYM(rel->r_info) != SHN_UNDEF) {
		symval = elf_get_symval(hdr, reltab->sh_link, ELF32_R_SYM(rel->r_info));
		if(symval == ELF_RELOC_ERR) return ELF_RELOC_ERR;
	}
    // Relocate based on type
	switch(ELF32_R_TYPE(rel->r_info)) {
		case R_386_NONE:
			// No relocation
			break;
		case R_386_32:
			// Symbol + Offset
			*ref = DO_386_32(symval, *ref);
			break;
		case R_386_PC32:
			// Symbol + Offset - Section Offset
			*ref = DO_386_PC32(symval, *ref, (int)ref);
			break;
		default:
			// Relocation type not supported, display error and return
			printk("Unsupported Relocation Type (%d).\n", ELF32_R_TYPE(rel->r_info));
			return ELF_RELOC_ERR;
	}
	return symval;
}

int elf_load_stage1(Elf32_Ehdr *hdr) {
	Elf32_Shdr *shdr = elf_sheader(hdr);

	// Iterate over section headers
	for(uint8_t i = 0; i < hdr->e_shnum; i++) {
		Elf32_Shdr *section = &shdr[i];
 
		// If the section isn't present in the file
		if(section->sh_type == SHT_NOBITS) {
			// Skip if it the section is empty
			if(!section->sh_size) continue;
			// If the section should appear in memory
			if(section->sh_flags & SHF_ALLOC) {
				// Allocate and zero some memory
				// void *mem = malloc(section->sh_size);
				void *mem = NULL;
				memset(mem, 0, section->sh_size);

				// Assign the memory offset to the section offset
				section->sh_offset = (int)mem - (int)hdr;
				printk("Allocated memory for a section (%ld).\n", section->sh_size);
			}
		}
	}
	return 0;
}

int elf_load_stage2(Elf32_Ehdr *hdr) {
	Elf32_Shdr *shdr = elf_sheader(hdr);
 
	unsigned int i, idx;
	// Iterate over section headers
	for(i = 0; i < hdr->e_shnum; i++) {
		Elf32_Shdr *section = &shdr[i];
 
		// If this is a relocation section
		if(section->sh_type == SHT_REL) {
			// Process each entry in the table
			for(idx = 0; idx < section->sh_size / section->sh_entsize; idx++) {
				Elf32_Rel *reltab = &((Elf32_Rel *)((int)hdr + section->sh_offset))[idx];
				int result = elf_do_reloc(hdr, reltab, section);
				// On error, display a message and return
				if(result == ELF_RELOC_ERR) {
					printk("Failed to relocate symbol.\n");
					return ELF_RELOC_ERR;
				}
			}
		}
	}
	return 0;
}

uint8_t elf_check_file(Elf32_Ehdr *hdr) {
	if(!hdr) return 0;
	if(hdr->e_ident[EI_MAG0] != ELFMAG0) {
		printk("ELF Header EI_MAG0 incorrect.\n");
		return 0;
	}
	if(hdr->e_ident[EI_MAG1] != ELFMAG1) {
		printk("ELF Header EI_MAG1 incorrect.\n");
		return 0;
	}
	if(hdr->e_ident[EI_MAG2] != ELFMAG2) {
		printk("ELF Header EI_MAG2 incorrect.\n");
		return 0;
	}
	if(hdr->e_ident[EI_MAG3] != ELFMAG3) {
		printk("ELF Header EI_MAG3 incorrect.\n");
		return 0;
	}
	return 1;
}

uint8_t elf_check_supported(Elf32_Ehdr *hdr) {
	if(!elf_check_file(hdr)) {
		printk("Invalid ELF File.\n");
		return 0;
	}
	if(hdr->e_ident[EI_CLASS] != ELFCLASS32) {
		printk("Unsupported ELF File Class.\n");
		return 0;
	}
	if(hdr->e_ident[EI_DATA] != ELFDATA2LSB) {
		printk("Unsupported ELF File byte order.\n");
		return 0;
	}
	if(hdr->e_machine != EM_ARM) {
		printk("Unsupported ELF File target.\n");
		return 0;
	}
	if(hdr->e_ident[EI_VERSION] != EV_CURRENT) {
		printk("Unsupported ELF File version.\n");
		return 0;
	}
	if(hdr->e_type != ET_REL && hdr->e_type != ET_EXEC) {
		printk("Unsupported ELF File type.\n");
		return 0;
	}
	return 1;
}

static void print_section_headers(file_info_t *elf_file, Elf32_Ehdr *hdr) {
	// Elf32_Shdr *shdr = elf_sheader(hdr);

	uint8_t string_table[0x53] = {0};
	read_file_content(*elf_file, 0x943c, 0x53, string_table);

	printk("String Table Index: %x \n", hdr->e_shstrndx);

	// Iterate over section headers
	for(uint8_t i = 0; i < hdr->e_shnum; i++) {
		uint8_t sec_header[0x40] = {0};
	    read_file_content(*elf_file, (hdr->e_shoff + i * sizeof(Elf32_Shdr)), sizeof(Elf32_Shdr), sec_header);
		Elf32_Shdr *section = (Elf32_Shdr *)&sec_header[0];
		printk("____***____\n Index: %x sh_name: %s sh_type: %x sh_flags: %x sh_addr: %x \n", i, &string_table[section->sh_name],
			section->sh_type, section->sh_flags, section->sh_addr
		);
		printk(" sh_offset: %x sh_size: %x sh_link: %x sh_info: %x \n", section->sh_offset,
			section->sh_size, section->sh_link, section->sh_info
		);
		printk(" sh_addralign: %x sh_entsize: %x \n____***____\n", section->sh_addralign,section->sh_entsize);
	}
}

int load_elf(file_info_t *elf_file) {
	uint8_t elf_hdr_b[52] = {0};
	printk(" \n  Elf file base: %x file_size: %x size: %x \n", elf_file->inode, elf_file->file_size, sizeof(Elf32_Ehdr));
	read_file_content(*elf_file, 0, sizeof(Elf32_Ehdr), elf_hdr_b);
	Elf32_Ehdr *elf_hdr = (Elf32_Ehdr *) &elf_hdr_b[0];
	if(!elf_check_file(elf_hdr)) {
		return 0;
	}
	if(!elf_check_supported(elf_hdr)) {
		return 0;
	}
	print_section_headers(elf_file, elf_hdr);
	printk("Returning");
	return 0;
}