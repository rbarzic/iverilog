/*
 * Copyright (c) 2001 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#if !defined(WINNT)
#ident "$Id: eval_expr.c,v 1.2 2001/03/23 01:10:24 steve Exp $"
#endif

# include  "vvp_priv.h"
# include  <assert.h>

struct vector_info draw_eval_expr_wid(ivl_expr_t exp, unsigned wid);

static unsigned char allocation_map[0x10000/8];

static inline int peek_bit(unsigned addr)
{
      unsigned bit = addr % 8;
      addr /= 8;
      return 1 & (allocation_map[addr] >> bit);
}

static inline void set_bit(unsigned addr)
{
      unsigned bit = addr % 8;
      addr /= 8;
      allocation_map[addr] |= (1 << bit);
}

static inline void clr_bit(unsigned addr)
{
      unsigned bit = addr % 8;
      addr /= 8;
      allocation_map[addr] &= ~(1 << bit);
}

static inline void clr_vector(struct vector_info vec)
{
      unsigned idx;
      for (idx = 0 ;  idx < vec.wid ;  idx += 1)
	    clr_bit(vec.base + idx);
}

static unsigned short allocate_vector(unsigned short wid)
{
      unsigned short base = 8;

      unsigned short idx = 0;
      while (idx < wid) {
	    assert((base + idx) < 0x10000);
	    if (peek_bit(base+idx)) {
		  base = base + idx + 1;
		  idx = 0;

	    } else {
		  idx += 1;
	    }
      }

      for (idx = 0 ;  idx < wid ;  idx += 1)
	    set_bit(base+idx);

      return base;
}


static struct vector_info draw_binary_expr_eq(ivl_expr_t exp)
{
      ivl_expr_t le = ivl_expr_oper1(exp);
      ivl_expr_t re = ivl_expr_oper2(exp);

      struct vector_info lv;
      struct vector_info rv;

      unsigned wid = ivl_expr_width(le);
      if (ivl_expr_width(re) > wid)
	    wid = ivl_expr_width(re);

      lv = draw_eval_expr_wid(le, wid);
      rv = draw_eval_expr_wid(re, wid);

      switch (ivl_expr_opcode(exp)) {
	  case 'e': /* == */
	    assert(lv.wid == rv.wid);
	    fprintf(vvp_out, "    %%cmp/u %u, %u, %u;\n", lv.base,
		    rv.base, lv.wid);
	    clr_vector(lv);
	    clr_vector(rv);
	    lv.base = 4;
	    lv.wid = 1;
	    break;

	  case 'n': /* != */
	    assert(lv.wid == rv.wid);
	    fprintf(vvp_out, "    %%cmp/u %u, %u, %u;\n", lv.base,
		    rv.base, lv.wid);
	    fprintf(vvp_out, "    %%inv 4, 1;\n");

	    clr_vector(lv);
	    clr_vector(rv);
	    lv.base = 4;
	    lv.wid = 1;
	    break;

	  default:
	    assert(0);
      }

      return lv;
}

static struct vector_info draw_binary_expr(ivl_expr_t exp, unsigned wid)
{
      struct vector_info rv;

      switch (ivl_expr_opcode(exp)) {
	  case 'e': /* == */
	  case 'n': /* != */
	    assert(wid == 1);
	    rv = draw_binary_expr_eq(exp);
	    break;

	  default:
	    assert(0);
      }

      return rv;
}

/*
 * A number in an expression is made up by copying constant bits into
 * the allocated vector.
 */
static struct vector_info draw_number_expr(ivl_expr_t exp, unsigned wid)
{
      unsigned idx;
      struct vector_info res;
      const char*bits = ivl_expr_bits(exp);

      res.wid  = wid;

      assert(ivl_expr_width(exp) >= wid);

	/* If all the bits of the number have the same value, then we
	   can use a constant bit. There is no need to allocate wr
	   bits, and there is no need to generate any code. */

      for (idx = 1 ;  idx < res.wid ;  idx += 1) {
	    if (bits[idx] != bits[0])
		  break;
      }

      if (idx == res.wid) {
	    switch (bits[0]) {
		case '0':
		  res.base = 0;
		  break;
		case '1':
		  res.base = 1;
		  break;
		case 'x':
		  res.base = 2;
		  break;
		case 'z':
		  res.base = 3;
		  break;
	    }
	    return res;
      }

	/* The number value needs to be represented as an allocated
	   vector. Allocate the vector and use %mov instructions to
	   load the constant bit values. */
      res.base = allocate_vector(wid);

      idx = 0;
      while (idx < wid) {
	    unsigned cnt;
	    char src = '?';
	    switch (bits[idx]) {
		case '0':
		  src = '0';
		  break;
		case '1':
		  src = '1';
		  break;
		case 'x':
		  src = '2';
		  break;
		case 'z':
		  src = '3';
		  break;
	    }

	    for (cnt = 1 ;  idx+cnt < wid ;  cnt += 1)
		  if (bits[idx+cnt] != bits[idx])
			break;

	    fprintf(vvp_out, "    %%mov %u, %c, %u;\n",
		    res.base+idx, src, cnt);

	    idx += cnt;
      }

      return res;
}

static struct vector_info draw_signal_expr(ivl_expr_t exp, unsigned wid)
{
      unsigned idx;
      unsigned swid = ivl_expr_width(exp);
      const char*name = ivl_expr_name(exp);
      struct vector_info res;

      if (swid > wid)
	    swid = wid;

      res.base = allocate_vector(wid);
      res.wid  = wid;

      for (idx = 0 ;  idx < swid ;  idx += 1)
	    fprintf(vvp_out, "    %%load  %u, V_%s[%u];\n",
		    res.base+idx, name, idx);

	/* Pad the signal value with zeros. */
      if (swid < wid)
	    fprintf(vvp_out, "    %%mov %u, 0, %u;\n",
		    res.base+swid, wid-swid);
      return res;
}

struct vector_info draw_eval_expr_wid(ivl_expr_t exp, unsigned wid)
{
      struct vector_info res;

      switch (ivl_expr_type(exp)) {
	  case IVL_EX_NONE:
	  default:
	    assert(0);
	    res.base = 0;
	    res.wid = 0;
	    break;

	  case IVL_EX_BINARY:
	    res = draw_binary_expr(exp, wid);
	    break;

	  case IVL_EX_NUMBER:
	    res = draw_number_expr(exp, wid);
	    break;

	  case IVL_EX_SIGNAL:
	    res = draw_signal_expr(exp, wid);
	    break;
      }

      return res;
}

struct vector_info draw_eval_expr(ivl_expr_t exp)
{
      return draw_eval_expr_wid(exp, ivl_expr_width(exp));
}

/*
 * $Log: eval_expr.c,v $
 * Revision 1.2  2001/03/23 01:10:24  steve
 *  Assure that operands are the correct width.
 *
 * Revision 1.1  2001/03/22 05:06:21  steve
 *  Geneate code for conditional statements.
 *
 */

