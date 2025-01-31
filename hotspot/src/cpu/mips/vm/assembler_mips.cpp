/*
 * Copyright (c) 1997, 2014, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2015, 2022, Loongson Technology. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "asm/assembler.hpp"
#include "asm/assembler.inline.hpp"
#include "gc_interface/collectedHeap.inline.hpp"
#include "interpreter/interpreter.hpp"
#include "memory/cardTableModRefBS.hpp"
#include "memory/resourceArea.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/interfaceSupport.hpp"
#include "runtime/objectMonitor.hpp"
#include "runtime/os.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#ifndef PRODUCT
#include "compiler/disassembler.hpp"
#endif
#if INCLUDE_ALL_GCS
#include "gc_implementation/g1/g1CollectedHeap.inline.hpp"
#include "gc_implementation/g1/g1SATBCardTableModRefBS.hpp"
#include "gc_implementation/g1/heapRegion.hpp"
#endif // INCLUDE_ALL_GCS

#ifdef PRODUCT
#define BLOCK_COMMENT(str) /* nothing */
#define STOP(error) stop(error)
#else
#define BLOCK_COMMENT(str) block_comment(str)
#define STOP(error) block_comment(error); stop(error)
#endif

#define BIND(label) bind(label); BLOCK_COMMENT(#label ":")

// Implementation of AddressLiteral

AddressLiteral::AddressLiteral(address target, relocInfo::relocType rtype) {
  _is_lval = false;
  _target = target;
  _rspec = rspec_from_rtype(rtype, target);
}

// Implementation of Address

Address Address::make_array(ArrayAddress adr) {
  AddressLiteral base = adr.base();
  Address index = adr.index();
  assert(index._disp == 0, "must not have disp"); // maybe it can?
  Address array(index._base, index._index, index._scale, (intptr_t) base.target());
  array._rspec = base._rspec;
  return array;
}

// exceedingly dangerous constructor
Address::Address(address loc, RelocationHolder spec) {
  _base  = noreg;
  _index = noreg;
  _scale = no_scale;
  _disp  = (intptr_t) loc;
  _rspec = spec;
}


#define A0 RA0
#define A1 RA1
#define A2 RA2
#define A3 RA3
#define A4 RA4
#define A5 RA5
#define A6 RA6
#define A7 RA7
#define T0 RT0
#define T1 RT1
#define T2 RT2
#define T3 RT3
#define T8 RT8
#define T9 RT9

// Implementation of Assembler
const char *Assembler::ops_name[] = {
  "special",  "regimm",   "j",      "jal",    "beq",      "bne",      "blez",   "bgtz",
  "addi",     "addiu",    "slti",   "sltiu",  "andi",     "ori",      "xori",   "lui",
  "cop0",     "cop1",     "cop2",   "cop3",   "beql",     "bnel",     "bleql",  "bgtzl",
  "daddi",    "daddiu",   "ldl",    "ldr",    "",         "",         "",       "",
  "lb",       "lh",       "lwl",    "lw",     "lbu",      "lhu",      "lwr",    "lwu",
  "sb",       "sh",       "swl",    "sw",     "sdl",      "sdr",      "swr",    "cache",
  "ll",       "lwc1",     "",       "",       "lld",      "ldc1",     "",       "ld",
  "sc",       "swc1",     "",       "",       "scd",      "sdc1",     "",       "sd"
};

const char* Assembler::special_name[] = {
  "sll",      "",         "srl",      "sra",      "sllv",     "",         "srlv",     "srav",
  "jr",       "jalr",     "movz",     "movn",     "syscall",  "break",    "",         "sync",
  "mfhi",     "mthi",     "mflo",     "mtlo",     "dsll",     "",         "dsrl",     "dsra",
  "mult",     "multu",    "div",      "divu",     "dmult",    "dmultu",   "ddiv",     "ddivu",
  "add",      "addu",     "sub",      "subu",     "and",      "or",       "xor",      "nor",
  "",         "",         "slt",      "sltu",     "dadd",     "daddu",    "dsub",     "dsubu",
  "tge",      "tgeu",     "tlt",      "tltu",     "teq",      "",         "tne",      "",
  "dsll",     "",         "dsrl",     "dsra",     "dsll32",   "",         "dsrl32",   "dsra32"
};

const char* Assembler::cop1_name[] = {
  "add",      "sub",      "mul",      "div",      "sqrt",     "abs",      "mov",      "neg",
  "round.l",  "trunc.l",  "ceil.l",   "floor.l",  "round.w",  "trunc.w",  "ceil.w",   "floor.w",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "c.f",      "c.un",     "c.eq",     "c.ueq",    "c.olt",    "c.ult",    "c.ole",    "c.ule",
  "c.sf",     "c.ngle",   "c.seq",    "c.ngl",    "c.lt",     "c.nge",    "c.le",     "c.ngt"
};

const char* Assembler::cop1x_name[] = {
  "lwxc1", "ldxc1",       "",         "",         "",    "luxc1",         "",         "",
  "swxc1", "sdxc1",       "",         "",         "",    "suxc1",         "",    "prefx",
  "",         "",         "",         "",         "",         "",  "alnv.ps",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "madd.s",   "madd.d",   "",         "",         "",         "",  "madd.ps",         "",
  "msub.s",   "msub.d",   "",         "",         "",         "",  "msub.ps",         "",
  "nmadd.s", "nmadd.d",   "",         "",         "",         "", "nmadd.ps",         "",
  "nmsub.s", "nmsub.d",   "",         "",         "",         "", "nmsub.ps",         ""
};

const char* Assembler::special2_name[] = {
  "madd",     "",         "mul",      "",         "msub",     "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "gsdmult",  "",         "",         "gsdiv",    "gsddiv",   "",         "",
  "",         "",         "",         "",         "gsmod",    "gsdmod",   "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         ""
};

const char* Assembler::special3_name[] = {
  "ext",      "",         "",         "",      "ins",    "dinsm",    "dinsu",     "dins",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "bshfl",    "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
  "",         "",         "",         "",         "",         "",         "",         "",
};

const char* Assembler::regimm_name[] = {
  "bltz",     "bgez",     "bltzl",    "bgezl",    "",         "",         "",         "",
  "tgei",     "tgeiu",    "tlti",     "tltiu",    "teqi",     "",         "tnei",     "",
  "bltzal",   "bgezal",   "bltzall",  "bgezall"
};

const char* Assembler::gs_ldc2_name[] = {
  "gslbx",    "gslhx",    "gslwx",    "gsldx",    "",         "",         "gslwxc1",  "gsldxc1"
};


const char* Assembler::gs_lwc2_name[] = {
        "",       "",       "",       "",         "",         "",         "",         "",
        "",       "",       "",       "",         "",         "",         "",         "",
        "gslble", "gslbgt", "gslhle", "gslhgt",   "gslwle",   "gslwgt",   "gsldle",   "gsldgt",
        "",       "",       "",       "gslwlec1", "gslwgtc1", "gsldlec1", "gsldgtc1", "",/*LWDIR, LWPTE, LDDIR and LDPTE have the same low 6 bits.*/
        "gslq",   ""
};

const char* Assembler::gs_sdc2_name[] = {
  "gssbx",    "gsshx",    "gsswx",    "gssdx",    "",         "",         "gsswxc1",  "gssdxc1"
};

const char* Assembler::gs_swc2_name[] = {
        "",        "",        "",        "",        "",          "",          "",         "",
        "",        "",        "",        "",        "",          "",          "",         "",
        "gssble",  "gssbgt",  "gsshle",  "gsshgt",  "gsswle",    "gsswgt",    "gssdle",   "gssdgt",
        "",        "",        "",        "",        "gsswlec1",  "gsswgtc1",  "gssdlec1", "gssdgtc1",
        "gssq",    ""
};

//misleading name, print only branch/jump instruction
void Assembler::print_instruction(int inst) {
  const char *s;
  switch( opcode(inst) ) {
  default:
    s = ops_name[opcode(inst)];
    break;
  case special_op:
    s = special_name[special(inst)];
    break;
  case regimm_op:
    s = special_name[rt(inst)];
    break;
  }

  ::tty->print("%s", s);
}

int Assembler::is_int_mask(int x) {
  int xx = x;
  int count = 0;

  while (x != 0) {
    x &= (x - 1);
    count++;
  }

  if ((1<<count) == (xx+1)) {
    return count;
  } else {
    return -1;
  }
}

int Assembler::is_jlong_mask(jlong x) {
  jlong  xx = x;
  int count = 0;

  while (x != 0) {
    x &= (x - 1);
    count++;
  }

  if ((1<<count) == (xx+1)) {
    return count;
  } else {
    return -1;
  }
}

//without check, maybe fixed
int Assembler::patched_branch(int dest_pos, int inst, int inst_pos) {
  int v = (dest_pos - inst_pos - 4)>>2;
  switch(opcode(inst)) {
  case j_op:
  case jal_op:
  case lui_op:
  case ori_op:
  case daddiu_op:
    ShouldNotReachHere();
    break;
  default:
    assert(is_simm16(v), "must be simm16");
#ifndef PRODUCT
    if(!is_simm16(v))
    {
      tty->print_cr("must be simm16");
      tty->print_cr("Inst: %x", inst);
    }
#endif

    v = low16(v);
    inst &= 0xffff0000;
    break;
  }

  return inst | v;
}

int Assembler::branch_destination(int inst, int pos) {
  int off;

  switch(opcode(inst)) {
  case j_op:
  case jal_op:
    assert(false, "should not use j/jal here");
    break;
  default:
    off = expand(low16(inst), 15);
    break;
  }

  return off ? pos + 4 + (off<<2) : 0;
}

int AbstractAssembler::code_fill_byte() {
  return 0x00;                  // illegal instruction 0x00000000
}

// Now the Assembler instruction (identical for 32/64 bits)

void Assembler::lb(Register rt, Address src) {
  assert(src.index() == NOREG, "index is unimplemented");
  lb(rt, src.base(), src.disp());
}

void Assembler::lbu(Register rt, Address src) {
  assert(src.index() == NOREG, "index is unimplemented");
  lbu(rt, src.base(), src.disp());
}

void Assembler::ld(Register rt, Address dst){
  Register src   = rt;
  Register base  = dst.base();
  Register index = dst.index();

  int scale = dst.scale();
  int disp  = dst.disp();

  if (index != noreg) {
    if (Assembler::is_simm16(disp)) {
      if ( UseLEXT1 && Assembler::is_simm(disp, 8) ) {
        if (scale == 0) {
          gsldx(src, base, index, disp);
        } else {
          dsll(AT, index, scale);
          gsldx(src, base, AT, disp);
        }
      } else {
        if (scale == 0) {
          daddu(AT, base, index);
        } else {
          dsll(AT, index, scale);
          daddu(AT, base, AT);
        }
        ld(src, AT, disp);
      }
    } else {
      if (scale == 0) {
        lui(AT, split_low(disp >> 16));
        if (split_low(disp)) ori(AT, AT, split_low(disp));
        daddu(AT, AT, base);
        if (UseLEXT1) {
          gsldx(src, AT, index, 0);
        } else {
          daddu(AT, AT, index);
          ld(src, AT, 0);
        }
      } else {
        assert_different_registers(src, AT);
        dsll(AT, index, scale);
        daddu(AT, base, AT);
        lui(src, split_low(disp >> 16));
        if (split_low(disp)) ori(src, src, split_low(disp));
        if (UseLEXT1) {
          gsldx(src, AT, src, 0);
        } else {
          daddu(AT, AT, src);
          ld(src, AT, 0);
        }
      }
    }
  } else {
    if (Assembler::is_simm16(disp)) {
      ld(src, base, disp);
    } else {
      lui(AT, split_low(disp >> 16));
      if (split_low(disp)) ori(AT, AT, split_low(disp));

      if (UseLEXT1) {
        gsldx(src, base, AT, 0);
      } else {
        daddu(AT, base, AT);
        ld(src, AT, 0);
      }
    }
  }
}

void Assembler::ldl(Register rt, Address src){
  assert(src.index() == NOREG, "index is unimplemented");
  ldl(rt, src.base(), src.disp());
}

void Assembler::ldr(Register rt, Address src){
  assert(src.index() == NOREG, "index is unimplemented");
  ldr(rt, src.base(), src.disp());
}

void Assembler::lh(Register rt, Address src){
  assert(src.index() == NOREG, "index is unimplemented");
  lh(rt, src.base(), src.disp());
}

void Assembler::lhu(Register rt, Address src){
  assert(src.index() == NOREG, "index is unimplemented");
  lhu(rt, src.base(), src.disp());
}

void Assembler::ll(Register rt, Address src){
  assert(src.index() == NOREG, "index is unimplemented");
  ll(rt, src.base(), src.disp());
}

void Assembler::lld(Register rt, Address src){
  assert(src.index() == NOREG, "index is unimplemented");
  lld(rt, src.base(), src.disp());
}

void Assembler::lw(Register rt, Address dst){
  Register src   = rt;
  Register base  = dst.base();
  Register index = dst.index();

  int scale = dst.scale();
  int disp  = dst.disp();

  if (index != noreg) {
    if (Assembler::is_simm16(disp)) {
      if ( UseLEXT1 && Assembler::is_simm(disp, 8) ) {
        if (scale == 0) {
          gslwx(src, base, index, disp);
        } else {
          dsll(AT, index, scale);
          gslwx(src, base, AT, disp);
        }
      } else {
        if (scale == 0) {
          daddu(AT, base, index);
        } else {
          dsll(AT, index, scale);
          daddu(AT, base, AT);
        }
        lw(src, AT, disp);
      }
    } else {
      if (scale == 0) {
        lui(AT, split_low(disp >> 16));
        if (split_low(disp)) ori(AT, AT, split_low(disp));
        daddu(AT, AT, base);
        if (UseLEXT1) {
          gslwx(src, AT, index, 0);
        } else {
          daddu(AT, AT, index);
          lw(src, AT, 0);
        }
      } else {
        assert_different_registers(src, AT);
        dsll(AT, index, scale);
        daddu(AT, base, AT);
        lui(src, split_low(disp >> 16));
        if (split_low(disp)) ori(src, src, split_low(disp));
        if (UseLEXT1) {
          gslwx(src, AT, src, 0);
        } else {
          daddu(AT, AT, src);
          lw(src, AT, 0);
        }
      }
    }
  } else {
    if (Assembler::is_simm16(disp)) {
      lw(src, base, disp);
    } else {
      lui(AT, split_low(disp >> 16));
      if (split_low(disp)) ori(AT, AT, split_low(disp));

      if (UseLEXT1) {
        gslwx(src, base, AT, 0);
      } else {
        daddu(AT, base, AT);
        lw(src, AT, 0);
      }
    }
  }
}

void Assembler::lea(Register rt, Address src) {
  Register dst   = rt;
  Register base  = src.base();
  Register index = src.index();

  int scale = src.scale();
  int disp  = src.disp();

  if (index == noreg) {
    if (is_simm16(disp)) {
      daddiu(dst, base, disp);
    } else {
      lui(AT, split_low(disp >> 16));
      if (split_low(disp)) ori(AT, AT, split_low(disp));
      daddu(dst, base, AT);
    }
  } else {
    if (scale == 0) {
      if (is_simm16(disp)) {
        daddu(AT, base, index);
        daddiu(dst, AT, disp);
      } else {
        lui(AT, split_low(disp >> 16));
        if (split_low(disp)) ori(AT, AT, split_low(disp));
        daddu(AT, base, AT);
        daddu(dst, AT, index);
      }
    } else {
      if (is_simm16(disp)) {
        dsll(AT, index, scale);
        daddu(AT, AT, base);
        daddiu(dst, AT, disp);
      } else {
        assert_different_registers(dst, AT);
        lui(AT, split_low(disp >> 16));
        if (split_low(disp)) ori(AT, AT, split_low(disp));
        daddu(AT, AT, base);
        dsll(dst, index, scale);
        daddu(dst, dst, AT);
      }
    }
  }
}

void Assembler::lwl(Register rt, Address src){
  assert(src.index() == NOREG, "index is unimplemented");
  lwl(rt, src.base(), src.disp());
}

void Assembler::lwr(Register rt, Address src){
  assert(src.index() == NOREG, "index is unimplemented");
  lwr(rt, src.base(), src.disp());
}

void Assembler::lwu(Register rt, Address src){
  assert(src.index() == NOREG, "index is unimplemented");
  lwu(rt, src.base(), src.disp());
}

void Assembler::sb(Register rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  sb(rt, dst.base(), dst.disp());
}

void Assembler::sc(Register rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  sc(rt, dst.base(), dst.disp());
}

void Assembler::scd(Register rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  scd(rt, dst.base(), dst.disp());
}

void Assembler::sd(Register rt, Address dst) {
  Register src   = rt;
  Register base  = dst.base();
  Register index = dst.index();

  int scale = dst.scale();
  int disp  = dst.disp();

  if (index != noreg) {
    if (is_simm16(disp)) {
      if ( UseLEXT1 && is_simm(disp, 8)) {
        if (scale == 0) {
          gssdx(src, base, index, disp);
        } else {
          assert_different_registers(rt, AT);
          dsll(AT, index, scale);
          gssdx(src, base, AT, disp);
        }
      } else {
        assert_different_registers(rt, AT);
        if (scale == 0) {
          daddu(AT, base, index);
        } else {
          dsll(AT, index, scale);
          daddu(AT, base, AT);
        }
        sd(src, AT, disp);
      }
    } else {
      assert_different_registers(rt, AT);
      if (scale == 0) {
        lui(AT, split_low(disp >> 16));
        if (split_low(disp)) ori(AT, AT, split_low(disp));
        daddu(AT, AT, base);
        if (UseLEXT1) {
          gssdx(src, AT, index, 0);
        } else {
          daddu(AT, AT, index);
          sd(src, AT, 0);
        }
      } else {
        daddiu(SP, SP, -wordSize);
        sd(T9, SP, 0);

        dsll(AT, index, scale);
        daddu(AT, base, AT);
        lui(T9, split_low(disp >> 16));
        if (split_low(disp)) ori(T9, T9, split_low(disp));
        daddu(AT, AT, T9);
        ld(T9, SP, 0);
        daddiu(SP, SP, wordSize);
        sd(src, AT, 0);
      }
    }
  } else {
    if (is_simm16(disp)) {
      sd(src, base, disp);
    } else {
      assert_different_registers(rt, AT);
      lui(AT, split_low(disp >> 16));
      if (split_low(disp)) ori(AT, AT, split_low(disp));

      if (UseLEXT1) {
        gssdx(src, base, AT, 0);
      } else {
        daddu(AT, base, AT);
        sd(src, AT, 0);
      }
    }
  }
}

void Assembler::sdl(Register rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  sdl(rt, dst.base(), dst.disp());
}

void Assembler::sdr(Register rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  sdr(rt, dst.base(), dst.disp());
}

void Assembler::sh(Register rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  sh(rt, dst.base(), dst.disp());
}

void Assembler::sw(Register rt, Address dst) {
  Register src   = rt;
  Register base  = dst.base();
  Register index = dst.index();

  int scale = dst.scale();
  int disp  = dst.disp();

  if (index != noreg) {
    if ( Assembler::is_simm16(disp) ) {
      if ( UseLEXT1 && Assembler::is_simm(disp, 8) ) {
        if (scale == 0) {
          gsswx(src, base, index, disp);
        } else {
          assert_different_registers(rt, AT);
          dsll(AT, index, scale);
          gsswx(src, base, AT, disp);
        }
      } else {
        assert_different_registers(rt, AT);
        if (scale == 0) {
          daddu(AT, base, index);
        } else {
          dsll(AT, index, scale);
          daddu(AT, base, AT);
        }
        sw(src, AT, disp);
      }
    } else {
      assert_different_registers(rt, AT);
      if (scale == 0) {
        lui(AT, split_low(disp >> 16));
        if (split_low(disp)) ori(AT, AT, split_low(disp));
        daddu(AT, AT, base);
        if (UseLEXT1) {
          gsswx(src, AT, index, 0);
        } else {
          daddu(AT, AT, index);
          sw(src, AT, 0);
        }
      } else {
        daddiu(SP, SP, -wordSize);
        sd(T9, SP, 0);

        dsll(AT, index, scale);
        daddu(AT, base, AT);
        lui(T9, split_low(disp >> 16));
        if (split_low(disp)) ori(T9, T9, split_low(disp));
        daddu(AT, AT, T9);
        ld(T9, SP, 0);
        daddiu(SP, SP, wordSize);
        sw(src, AT, 0);
      }
    }
  } else {
    if (Assembler::is_simm16(disp)) {
      sw(src, base, disp);
    } else {
      assert_different_registers(rt, AT);
      lui(AT, split_low(disp >> 16));
      if (split_low(disp)) ori(AT, AT, split_low(disp));

      if (UseLEXT1) {
        gsswx(src, base, AT, 0);
      } else {
        daddu(AT, base, AT);
        sw(src, AT, 0);
      }
    }
  }
}

void Assembler::swl(Register rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  swl(rt, dst.base(), dst.disp());
}

void Assembler::swr(Register rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  swr(rt, dst.base(), dst.disp());
}

void Assembler::lwc1(FloatRegister rt, Address src) {
  assert(src.index() == NOREG, "index is unimplemented");
  lwc1(rt, src.base(), src.disp());
}

void Assembler::ldc1(FloatRegister rt, Address src) {
  assert(src.index() == NOREG, "index is unimplemented");
  ldc1(rt, src.base(), src.disp());
}

void Assembler::swc1(FloatRegister rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  swc1(rt, dst.base(), dst.disp());
}

void Assembler::sdc1(FloatRegister rt, Address dst) {
  assert(dst.index() == NOREG, "index is unimplemented");
  sdc1(rt, dst.base(), dst.disp());
}

void Assembler::j(address entry) {
  int dest = ((intptr_t)entry & (intptr_t)0xfffffff)>>2;
  emit_long((j_op<<26) | dest);
  has_delay_slot();
}

void Assembler::jal(address entry) {
  int dest = ((intptr_t)entry & (intptr_t)0xfffffff)>>2;
  emit_long((jal_op<<26) | dest);
  has_delay_slot();
}

void Assembler::emit_long(int x) { // shadows AbstractAssembler::emit_long
  check_delay();
  AbstractAssembler::emit_int32(x);
  if (PatchContinuousLoad)
    check_load_and_patch(x);
}

inline void Assembler::emit_data(int x) { emit_long(x); }
inline void Assembler::emit_data(int x, relocInfo::relocType rtype) {
  relocate(rtype);
  emit_long(x);
}

inline void Assembler::emit_data(int x, RelocationHolder const& rspec) {
  relocate(rspec);
  emit_long(x);
}

inline void Assembler::check_delay() {
#ifdef CHECK_DELAY
  guarantee(delay_state != at_delay_slot, "must say delayed() when filling delay slot");
  delay_state = no_delay;
#endif
}

// Check If an Instruction Is a Load Instruction

// All load instructions includes:
// 1. FIRST OPS
//   LDL, LDR, LB, LH, LWL, LW, LBU, LHU, LWR, LWU, LD, LWC1, PREF, LDC1
// 2. SPECIAL OPS
//   MOVF, MOVT
// 3. COP0 OPS
//   MFC0, DMFC0, MFGC0, DMFGC0
// 4. COP1 OPS
//   MFC1, DMFC1, CFC1, MFHC1, MTC1, DMTC1, CTC1, MTHC1, MOVZ.FMT, MOVN.FMT
// 5. COP1X OPS
//   LWXC1, LDXC1, LUXC1, PREFX
// 6. SPECIAL2 OPs
//   CAMPV, CAMPI, RAMRI
// 7. SPECIAL3 OPS
//   LWX, LHX, LBUX, LDX, RDHWR
// 8. LWC2 OPS
//   GSLWLC1, GSLWRC1, GSLDLC1, GSLDRC1, GSLBLE, GSLBGT,
//   GSLHLE, GSLHGT, GSLWLE, GSLWGT, GSLDLE, GSLDGT,
//   LWDIR, LWPTE, LDDIR, LDPTE
//   GSLWLEC1, GSLWGTC1, GSLDLEC1, GSLDGTC1
// 9. LDC2 OPS
//   ALL LDC2 OPS(GSLBX, GSLHX, GSLWX, GSLDX, GSLWXC1, GSLDXC1)

#define SPECIAL_MOVCI_OP_MASK 0b1111110000000100000011111111111
bool Assembler::is_load_op(int x) {
  assert(PatchContinuousLoad, "just checking");
  int ins = x;
  int op = (ins >> 26) & 0x3f;
  switch (op) {
    //first_ops: ldl, ldr, lb, lh, lwl, lw, lbu, lhu, lwr, lwu, ld, lwc1, pref, ldc1, ldc2_ops
    case ldl_op:
    case ldr_op:
    case lb_op:
    case lh_op:
    case lwl_op:
    case lw_op:
    case lbu_op:
    case lhu_op:
    case lwr_op:
    case lwu_op:
    case ld_op:
    case lwc1_op:
    case pref_op:
    case ldc1_op:
    //ldc2_ops: gslbx,  gslhx,  gslwx,  gsldx, gslwxc1, gsldxc1
    case gs_ldc2_op:
      return true;
    //special_ops: movf, movt
    case special_op:
      if ((ins & SPECIAL_MOVCI_OP_MASK) == movci_op)
        return true;
      else
        return false;
    //cop0_ops: mfc0, dmfc0, mfgc0, dmfgc0
    case cop0_op:
      switch ((ins >> 21) & 0x1f) {
        case mfc0_op:
        case dmfc0_op:
          return true;
        case mxgc0_op:
          if ((ins >> 9 & 1) == 0)
            return true;
        default:
          return false;
      }
    //cop1_ops: mfc1, dmfc1, cfc1, mfhc1, mtc1, dmtc1, ctc1, mthc1, movz.fmt, movn.fmt
    case cop1_op:
      switch ((ins >> 21) & 0x1f) {
        case cfc1_op:
        case mfhc1_op:
        case mtc1_op:
        case dmtc1_op:
        case ctc1_op:
        case mthc1_op:
          return true;
        case single_fmt:
        case double_fmt:
        case ps_fmt:
          if ((ins & 0x3f == movz_f_op) || (ins & 0x3f == movn_f_op))
            return true;
        default:
          return false;
      }
    //cop1x_ops: lwxc1, ldxc1, luxc1, prefx
    case cop1x_op:
      switch (ins & 0x3f) {
        case lwxc1_op:
        case ldxc1_op:
        case luxc1_op:
        case prefx_op:
          return true;
        default:
          return false;
      }
    //special2_ops: campv, campi, ramri
    case special2_op:
      switch (ins & 0xff) {
        case campv_op << 6 | gscam_op:
        case campi_op << 6 | gscam_op:
        case ramri_op << 6 | gscam_op:
          return true;
        default:
          return false;
      }
    //special3_ops: lwx, lhx, lbux, ldx, rdhwr
    case special3_op:
      switch (ins & 0x3f) {
        case lxx_op:
        case rdhwr_op:
          return true;
        default:
          return false;
      }
    //lwc2_ops: gslwlc1, gslwrc1, gsldlc1,  gsldrc1,  gslble,   gslbgt,   gslhle, gslhgt, gslwle, gslwgt,
    //           gsldle,  gsldgt, gslwlec1, gslwgtc1, gsldlec1, gsldgtc1
    case gs_lwc2_op:
      if ((ins >> 5 & 1) == 0) //gslq, gslqc1 are excluded.
        return true;
      else
        return false;
    default:
      return false;
  }
  return false;
}

#define MAX_LOADS_INSTRUCTION_SEQUENCE_LEN 3
void Assembler::check_load_and_patch(int x) {
  int load_count = 0;
  assert(PatchContinuousLoad, "just checking");
  if (is_load_op(x)) {
    load_count = code()->get_continuous_load_instuctions_count();
    code()->set_continuous_load_instuctions_count(++load_count);
    if (load_count >= MAX_LOADS_INSTRUCTION_SEQUENCE_LEN) {
      assert(load_count == MAX_LOADS_INSTRUCTION_SEQUENCE_LEN, "load_count should not be greater than MAX_LOADS_INSTRUCTION_SEQUENCE_LEN");
#ifndef PRODUCT
      if (code()->get_continuous_load_instuctions_count() != MAX_LOADS_INSTRUCTION_SEQUENCE_LEN)
        tty->print_cr("get_continuous_load_instuctions_count() returns %d, which should be %d", code()->get_continuous_load_instuctions_count(), MAX_LOADS_INSTRUCTION_SEQUENCE_LEN);
      if (TracePatchContinuousLoad && PatchContinuousLoad) {
        tty->print_cr("found %d consecutive loads, separated by a nop, pc: %p", load_count, pc());
        int i;
        for (i = -MAX_LOADS_INSTRUCTION_SEQUENCE_LEN; i <= 0; i++)
          tty->print_cr("loads %d(" INTPTR_FORMAT "-" INTPTR_FORMAT "): ", i, p2i(pc()+(i*4)), p2i(pc()+(i*4)+4));
        Disassembler::decode(pc()+(i*4), pc()+(i*4)+4, tty);
        tty->print_cr("    -> nop");
        Disassembler::decode((address)&x, (address)&x+4, tty);
      }
#endif
      nop(); /*do a patch here, consecutive loads separated by a nop*/
      code()->set_continuous_load_instuctions_count(0);
    }
  } else {
    code()->set_continuous_load_instuctions_count(0);
  }
#ifndef PRODUCT
  load_count = code()->get_continuous_load_instuctions_count();
  if (load_count >= MAX_LOADS_INSTRUCTION_SEQUENCE_LEN)
    tty->print_cr("wrong load_count: %d", load_count);
  assert(load_count < MAX_LOADS_INSTRUCTION_SEQUENCE_LEN, "just checking");
#endif
}
