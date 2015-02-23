/* Target-dependent code for FreeBSD/i386.

   Copyright (C) 2003-2015 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "arch-utils.h"
#include "gdbcore.h"
#include "osabi.h"
#include "regcache.h"
#include "regset.h"
#include "i386fbsd-tdep.h"
#include "x86-xstate.h"

#include "i386-tdep.h"
#include "i387-tdep.h"
#include "fbsd-tdep.h"
#include "solib-svr4.h"

/* FreeBSD 3.0-RELEASE or later.  */

/* Supported register note sections.  */
static struct core_regset_section i386fbsd_regset_sections[] =
{
  { ".reg", 19 * 4, "general-purpose" },
  { ".reg2", 512, "floating-point" },
  { ".reg-xstate", X86_XSTATE_MAX_SIZE, "XSAVE extended state" },
  { NULL, 0 }
};

/* From <machine/reg.h>.  */
static int i386fbsd_r_reg_offset[] =
{
  9 * 4, 8 * 4, 7 * 4, 6 * 4,	/* %eax, %ecx, %edx, %ebx */
  15 * 4, 4 * 4,		/* %esp, %ebp */
  3 * 4, 2 * 4,			/* %esi, %edi */
  12 * 4, 14 * 4,		/* %eip, %eflags */
  13 * 4, 16 * 4,		/* %cs, %ss */
  1 * 4, 0 * 4, -1, -1		/* %ds, %es, %fs, %gs */
};

/* Sigtramp routine location.  */
CORE_ADDR i386fbsd_sigtramp_start_addr = 0xbfbfdf20;
CORE_ADDR i386fbsd_sigtramp_end_addr = 0xbfbfdff0;

/* From <machine/signal.h>.  */
int i386fbsd_sc_reg_offset[] =
{
  8 + 14 * 4,			/* %eax */
  8 + 13 * 4,			/* %ecx */
  8 + 12 * 4,			/* %edx */
  8 + 11 * 4,			/* %ebx */
  8 + 0 * 4,                    /* %esp */
  8 + 1 * 4,                    /* %ebp */
  8 + 10 * 4,                   /* %esi */
  8 + 9 * 4,                    /* %edi */
  8 + 3 * 4,                    /* %eip */
  8 + 4 * 4,                    /* %eflags */
  8 + 7 * 4,                    /* %cs */
  8 + 8 * 4,                    /* %ss */
  8 + 6 * 4,                    /* %ds */
  8 + 5 * 4,                    /* %es */
  8 + 15 * 4,			/* %fs */
  8 + 16 * 4			/* %gs */
};

/* From /usr/src/lib/libc/i386/gen/_setjmp.S.  */
static int i386fbsd_jmp_buf_reg_offset[] =
{
  -1,				/* %eax */
  -1,				/* %ecx */
  -1,				/* %edx */
  1 * 4,			/* %ebx */
  2 * 4,			/* %esp */
  3 * 4,			/* %ebp */
  4 * 4,			/* %esi */
  5 * 4,			/* %edi */
  0 * 4				/* %eip */
};

/* Get XSAVE extended state xcr0 from core dump.  */

uint64_t
i386fbsd_core_read_xcr0 (bfd *abfd)
{
  asection *xstate = bfd_get_section_by_name (abfd, ".reg-xstate");
  uint64_t xcr0;

  if (xstate)
    {
      size_t size = bfd_section_size (abfd, xstate);

      /* Check extended state size.  */
      if (size < X86_XSTATE_AVX_SIZE)
	xcr0 = X86_XSTATE_SSE_MASK;
      else
	{
	  char contents[8];

	  if (! bfd_get_section_contents (abfd, xstate, contents,
					  I386_FBSD_XSAVE_XCR0_OFFSET,
					  8))
	    {
	      warning (_("Couldn't read `xcr0' bytes from "
			 "`.reg-xstate' section in core file."));
	      return 0;
	    }

	  xcr0 = bfd_get_64 (abfd, contents);
	}
    }
  else
    xcr0 = 0;

  return xcr0;
}

static const struct target_desc *
i386fbsd_core_read_description (struct gdbarch *gdbarch,
				struct target_ops *target,
				bfd *abfd)
{
  uint64_t xcr0 = i386fbsd_core_read_xcr0 (abfd);

  switch (xcr0 & X86_XSTATE_ALL_MASK)
    {
    case X86_XSTATE_MPX_AVX512_MASK:
    case X86_XSTATE_AVX512_MASK:
      return tdesc_i386_avx512;
    case X86_XSTATE_MPX_MASK:
      return tdesc_i386_mpx;
    case X86_XSTATE_AVX_MASK:
      return tdesc_i386_avx;
    default:
      return tdesc_i386;
    }
}

static void
i386fbsdaout_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Obviously FreeBSD is BSD-based.  */
  i386bsd_init_abi (info, gdbarch);

  /* FreeBSD has a different `struct reg', and reserves some space for
     its FPU emulator in `struct fpreg'.  */
  tdep->gregset_reg_offset = i386fbsd_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (i386fbsd_r_reg_offset);
  tdep->sizeof_gregset = 18 * 4;
  tdep->sizeof_fpregset = 176;

  /* FreeBSD uses -freg-struct-return by default.  */
  tdep->struct_return = reg_struct_return;

  /* FreeBSD uses a different memory layout.  */
  tdep->sigtramp_start = i386fbsd_sigtramp_start_addr;
  tdep->sigtramp_end = i386fbsd_sigtramp_end_addr;

  /* FreeBSD has a more complete `struct sigcontext'.  */
  tdep->sc_reg_offset = i386fbsd_sc_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (i386fbsd_sc_reg_offset);
}

static void
i386fbsd_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  /* It's almost identical to FreeBSD a.out.  */
  i386fbsdaout_init_abi (info, gdbarch);

  /* Except that it uses ELF.  */
  i386_elf_init_abi (info, gdbarch);

  /* FreeBSD ELF uses SVR4-style shared libraries.  */
  set_solib_svr4_fetch_link_map_offsets
    (gdbarch, svr4_ilp32_fetch_link_map_offsets);
}

/* FreeBSD 4.0-RELEASE or later.  */

/* From <machine/reg.h>.  */
static int i386fbsd4_r_reg_offset[] =
{
  10 * 4, 9 * 4, 8 * 4, 7 * 4,	/* %eax, %ecx, %edx, %ebx */
  16 * 4, 5 * 4,		/* %esp, %ebp */
  4 * 4, 3 * 4,			/* %esi, %edi */
  13 * 4, 15 * 4,		/* %eip, %eflags */
  14 * 4, 17 * 4,		/* %cs, %ss */
  2 * 4, 1 * 4, 0 * 4, 18 * 4	/* %ds, %es, %fs, %gs */
};

/* From <machine/signal.h>.  */
int i386fbsd4_sc_reg_offset[] =
{
  20 + 11 * 4,			/* %eax */
  20 + 10 * 4,			/* %ecx */
  20 + 9 * 4,			/* %edx */
  20 + 8 * 4,			/* %ebx */
  20 + 17 * 4,			/* %esp */
  20 + 6 * 4,			/* %ebp */
  20 + 5 * 4,			/* %esi */
  20 + 4 * 4,			/* %edi */
  20 + 14 * 4,			/* %eip */
  20 + 16 * 4,			/* %eflags */
  20 + 15 * 4,			/* %cs */
  20 + 18 * 4,			/* %ss */
  20 + 3 * 4,			/* %ds */
  20 + 2 * 4,			/* %es */
  20 + 1 * 4,			/* %fs */
  20 + 0 * 4			/* %gs */
};

static void
i386fbsd4_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Generic FreeBSD support. */
  fbsd_init_abi (info, gdbarch);

  /* Inherit stuff from older releases.  We assume that FreeBSD
     4.0-RELEASE always uses ELF.  */
  i386fbsd_init_abi (info, gdbarch);

  /* FreeBSD 4.0 introduced a new `struct reg'.  */
  tdep->gregset_reg_offset = i386fbsd4_r_reg_offset;
  tdep->gregset_num_regs = ARRAY_SIZE (i386fbsd4_r_reg_offset);
  tdep->sizeof_gregset = 19 * 4;

  /* FreeBSD 4.0 introduced a new `struct sigcontext'.  */
  tdep->sc_reg_offset = i386fbsd4_sc_reg_offset;
  tdep->sc_num_regs = ARRAY_SIZE (i386fbsd4_sc_reg_offset);

  tdep->xsave_xcr0_offset = I386_FBSD_XSAVE_XCR0_OFFSET;

  /* Install supported register note sections.  */
  set_gdbarch_core_regset_sections (gdbarch, i386fbsd_regset_sections);

  set_gdbarch_core_read_description (gdbarch,
				     i386fbsd_core_read_description);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_i386fbsd_tdep (void);

void
_initialize_i386fbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_FREEBSD_AOUT,
			  i386fbsdaout_init_abi);
  gdbarch_register_osabi (bfd_arch_i386, 0, GDB_OSABI_FREEBSD_ELF,
			  i386fbsd4_init_abi);
}
