/* Linux driver for Philips webcam
   (C) 2004      Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


/* This tables contains entries for the 675/680/690 (Timon) camera, with
   4 different qualities (no compression, low, medium, high).
   It lists the bandwidth requirements for said mode by its alternate interface
   number. An alternate of 0 means that the mode is unavailable.

   There are 6 * 4 * 4 entries:
     6 different resolutions subqcif, qsif, qcif, sif, cif, vga
     6 framerates: 5, 10, 15, 20, 25, 30
     4 compression modi: none, low, medium, high

   When an uncompressed mode is not available, the next available compressed mode
   will be chosen (unless the decompressor is absent). Sometimes there are only
   1 or 2 compressed modes available; in that case entries are duplicated.
*/

#include "pwc-timon.h"

const struct Timon_table_entry Timon_table[PSZ_MAX][6][4] =
{
   /* SQCIF */
   {
      /* 5 fps */
      {
	 {1, 140,    0, {0x05, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x8C, 0xFC, 0x80, 0x02}},
	 {1, 140,    0, {0x05, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x8C, 0xFC, 0x80, 0x02}},
	 {1, 140,    0, {0x05, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x8C, 0xFC, 0x80, 0x02}},
	 {1, 140,    0, {0x05, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x8C, 0xFC, 0x80, 0x02}},
      },
      /* 10 fps */
      {
	 {2, 280,    0, {0x04, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x18, 0xA9, 0x80, 0x02}},
	 {2, 280,    0, {0x04, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x18, 0xA9, 0x80, 0x02}},
	 {2, 280,    0, {0x04, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x18, 0xA9, 0x80, 0x02}},
	 {2, 280,    0, {0x04, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x18, 0xA9, 0x80, 0x02}},
      },
      /* 15 fps */
      {
	 {3, 410,    0, {0x03, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x9A, 0x71, 0x80, 0x02}},
	 {3, 410,    0, {0x03, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x9A, 0x71, 0x80, 0x02}},
	 {3, 410,    0, {0x03, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x9A, 0x71, 0x80, 0x02}},
	 {3, 410,    0, {0x03, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x9A, 0x71, 0x80, 0x02}},
      },
      /* 20 fps */
      {
	 {4, 559,    0, {0x02, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x2F, 0x56, 0x80, 0x02}},
	 {4, 559,    0, {0x02, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x2F, 0x56, 0x80, 0x02}},
	 {4, 559,    0, {0x02, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x2F, 0x56, 0x80, 0x02}},
	 {4, 559,    0, {0x02, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x2F, 0x56, 0x80, 0x02}},
      },
      /* 25 fps */
      {
	 {5, 659,    0, {0x01, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x93, 0x46, 0x80, 0x02}},
	 {5, 659,    0, {0x01, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x93, 0x46, 0x80, 0x02}},
	 {5, 659,    0, {0x01, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x93, 0x46, 0x80, 0x02}},
	 {5, 659,    0, {0x01, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x93, 0x46, 0x80, 0x02}},
      },
      /* 30 fps */
      {
	 {7, 838,    0, {0x00, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x46, 0x3B, 0x80, 0x02}},
	 {7, 838,    0, {0x00, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x46, 0x3B, 0x80, 0x02}},
	 {7, 838,    0, {0x00, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x46, 0x3B, 0x80, 0x02}},
	 {7, 838,    0, {0x00, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x46, 0x3B, 0x80, 0x02}},
      },
   },
   /* QSIF */
   {
      /* 5 fps */
      {
	 {1, 146,    0, {0x2D, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x92, 0xFC, 0xC0, 0x02}},
	 {1, 146,    0, {0x2D, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x92, 0xFC, 0xC0, 0x02}},
	 {1, 146,    0, {0x2D, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x92, 0xFC, 0xC0, 0x02}},
	 {1, 146,    0, {0x2D, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x92, 0xFC, 0xC0, 0x02}},
      },
      /* 10 fps */
      {
	 {2, 291,    0, {0x2C, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x23, 0xA1, 0xC0, 0x02}},
	 {1, 191,  630, {0x2C, 0xF4, 0x05, 0x13, 0xA9, 0x12, 0xE1, 0x17, 0x08, 0xBF, 0xF4, 0xC0, 0x02}},
	 {1, 191,  630, {0x2C, 0xF4, 0x05, 0x13, 0xA9, 0x12, 0xE1, 0x17, 0x08, 0xBF, 0xF4, 0xC0, 0x02}},
	 {1, 191,  630, {0x2C, 0xF4, 0x05, 0x13, 0xA9, 0x12, 0xE1, 0x17, 0x08, 0xBF, 0xF4, 0xC0, 0x02}},
      },
      /* 15 fps */
      {
	 {3, 437,    0, {0x2B, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0xB5, 0x6D, 0xC0, 0x02}},
	 {2, 291,  640, {0x2B, 0xF4, 0x05, 0x13, 0xF7, 0x13, 0x2F, 0x13, 0x08, 0x23, 0xA1, 0xC0, 0x02}},
	 {2, 291,  640, {0x2B, 0xF4, 0x05, 0x13, 0xF7, 0x13, 0x2F, 0x13, 0x08, 0x23, 0xA1, 0xC0, 0x02}},
	 {1, 191,  420, {0x2B, 0xF4, 0x0D, 0x0D, 0x1B, 0x0C, 0x53, 0x1E, 0x08, 0xBF, 0xF4, 0xC0, 0x02}},
      },
      /* 20 fps */
      {
	 {4, 588,    0, {0x2A, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x4C, 0x52, 0xC0, 0x02}},
	 {3, 447,  730, {0x2A, 0xF4, 0x05, 0x16, 0xC9, 0x16, 0x01, 0x0E, 0x18, 0xBF, 0x69, 0xC0, 0x02}},
	 {2, 292,  476, {0x2A, 0xF4, 0x0D, 0x0E, 0xD8, 0x0E, 0x10, 0x19, 0x18, 0x24, 0xA1, 0xC0, 0x02}},
	 {1, 192,  312, {0x2A, 0xF4, 0x1D, 0x09, 0xB3, 0x08, 0xEB, 0x1E, 0x18, 0xC0, 0xF4, 0xC0, 0x02}},
      },
      /* 25 fps */
      {
	 {5, 703,    0, {0x29, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0xBF, 0x42, 0xC0, 0x02}},
	 {3, 447,  610, {0x29, 0xF4, 0x05, 0x13, 0x0B, 0x12, 0x43, 0x14, 0x18, 0xBF, 0x69, 0xC0, 0x02}},
	 {2, 292,  398, {0x29, 0xF4, 0x0D, 0x0C, 0x6C, 0x0B, 0xA4, 0x1E, 0x18, 0x24, 0xA1, 0xC0, 0x02}},
	 {1, 192,  262, {0x29, 0xF4, 0x25, 0x08, 0x23, 0x07, 0x5B, 0x1E, 0x18, 0xC0, 0xF4, 0xC0, 0x02}},
      },
      /* 30 fps */
      {
	 {8, 873,    0, {0x28, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x69, 0x37, 0xC0, 0x02}},
	 {5, 704,  774, {0x28, 0xF4, 0x05, 0x18, 0x21, 0x17, 0x59, 0x0F, 0x18, 0xC0, 0x42, 0xC0, 0x02}},
	 {3, 448,  492, {0x28, 0xF4, 0x05, 0x0F, 0x5D, 0x0E, 0x95, 0x15, 0x18, 0xC0, 0x69, 0xC0, 0x02}},
	 {2, 291,  320, {0x28, 0xF4, 0x1D, 0x09, 0xFB, 0x09, 0x33, 0x1E, 0x18, 0x23, 0xA1, 0xC0, 0x02}},
      },
   },
   /* QCIF */
   {
      /* 5 fps */
      {
	 {1, 193,    0, {0x0D, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0xC1, 0xF4, 0xC0, 0x02}},
	 {1, 193,    0, {0x0D, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0xC1, 0xF4, 0xC0, 0x02}},
	 {1, 193,    0, {0x0D, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0xC1, 0xF4, 0xC0, 0x02}},
	 {1, 193,    0, {0x0D, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0xC1, 0xF4, 0xC0, 0x02}},
      },
      /* 10 fps */
      {
	 {3, 385,    0, {0x0C, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x81, 0x79, 0xC0, 0x02}},
	 {2, 291,  800, {0x0C, 0xF4, 0x05, 0x18, 0xF4, 0x18, 0x18, 0x11, 0x08, 0x23, 0xA1, 0xC0, 0x02}},
	 {2, 291,  800, {0x0C, 0xF4, 0x05, 0x18, 0xF4, 0x18, 0x18, 0x11, 0x08, 0x23, 0xA1, 0xC0, 0x02}},
	 {1, 194,  532, {0x0C, 0xF4, 0x05, 0x10, 0x9A, 0x0F, 0xBE, 0x1B, 0x08, 0xC2, 0xF0, 0xC0, 0x02}},
      },
      /* 15 fps */
      {
	 {4, 577,    0, {0x0B, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x41, 0x52, 0xC0, 0x02}},
	 {3, 447,  818, {0x0B, 0xF4, 0x05, 0x19, 0x89, 0x18, 0xAD, 0x0F, 0x10, 0xBF, 0x69, 0xC0, 0x02}},
	 {2, 292,  534, {0x0B, 0xF4, 0x05, 0x10, 0xA3, 0x0F, 0xC7, 0x19, 0x10, 0x24, 0xA1, 0xC0, 0x02}},
	 {1, 195,  356, {0x0B, 0xF4, 0x15, 0x0B, 0x11, 0x0A, 0x35, 0x1E, 0x10, 0xC3, 0xF0, 0xC0, 0x02}},
      },
      /* 20 fps */
      {
	 {6, 776,    0, {0x0A, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x08, 0x3F, 0xC0, 0x02}},
	 {4, 591,  804, {0x0A, 0xF4, 0x05, 0x19, 0x1E, 0x18, 0x42, 0x0F, 0x18, 0x4F, 0x4E, 0xC0, 0x02}},
	 {3, 447,  608, {0x0A, 0xF4, 0x05, 0x12, 0xFD, 0x12, 0x21, 0x15, 0x18, 0xBF, 0x69, 0xC0, 0x02}},
	 {2, 291,  396, {0x0A, 0xF4, 0x15, 0x0C, 0x5E, 0x0B, 0x82, 0x1E, 0x18, 0x23, 0xA1, 0xC0, 0x02}},
      },
      /* 25 fps */
      {
	 {9, 928,    0, {0x09, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0xA0, 0x33, 0xC0, 0x02}},
	 {5, 703,  800, {0x09, 0xF4, 0x05, 0x18, 0xF4, 0x18, 0x18, 0x10, 0x18, 0xBF, 0x42, 0xC0, 0x02}},
	 {3, 447,  508, {0x09, 0xF4, 0x0D, 0x0F, 0xD2, 0x0E, 0xF6, 0x1B, 0x18, 0xBF, 0x69, 0xC0, 0x02}},
	 {2, 292,  332, {0x09, 0xF4, 0x1D, 0x0A, 0x5A, 0x09, 0x7E, 0x1E, 0x18, 0x24, 0xA1, 0xC0, 0x02}},
      },
      /* 30 fps */
      {
	 {0, },
	 {9, 956,  876, {0x08, 0xF4, 0x05, 0x1B, 0x58, 0x1A, 0x7C, 0x0E, 0x20, 0xBC, 0x33, 0x10, 0x02}},
	 {4, 592,  542, {0x08, 0xF4, 0x05, 0x10, 0xE4, 0x10, 0x08, 0x17, 0x20, 0x50, 0x4E, 0x10, 0x02}},
	 {2, 291,  266, {0x08, 0xF4, 0x25, 0x08, 0x48, 0x07, 0x6C, 0x1E, 0x20, 0x23, 0xA1, 0x10, 0x02}},
      },
   },
   /* SIF */
   {
      /* 5 fps */
      {
	 {4, 582,    0, {0x35, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x46, 0x52, 0x60, 0x02}},
	 {3, 387, 1276, {0x35, 0xF4, 0x05, 0x27, 0xD8, 0x26, 0x48, 0x03, 0x10, 0x83, 0x79, 0x60, 0x02}},
	 {2, 291,  960, {0x35, 0xF4, 0x0D, 0x1D, 0xF2, 0x1C, 0x62, 0x04, 0x10, 0x23, 0xA1, 0x60, 0x02}},
	 {1, 191,  630, {0x35, 0xF4, 0x1D, 0x13, 0xA9, 0x12, 0x19, 0x05, 0x08, 0xBF, 0xF4, 0x60, 0x02}},
      },
      /* 10 fps */
      {
	 {0, },
	 {6, 775, 1278, {0x34, 0xF4, 0x05, 0x27, 0xE8, 0x26, 0x58, 0x05, 0x30, 0x07, 0x3F, 0x10, 0x02}},
	 {3, 447,  736, {0x34, 0xF4, 0x15, 0x16, 0xFB, 0x15, 0x6B, 0x05, 0x18, 0xBF, 0x69, 0x10, 0x02}},
	 {2, 291,  480, {0x34, 0xF4, 0x2D, 0x0E, 0xF9, 0x0D, 0x69, 0x09, 0x18, 0x23, 0xA1, 0x10, 0x02}},
      },
      /* 15 fps */
      {
	 {0, },
	 {9, 955, 1050, {0x33, 0xF4, 0x05, 0x20, 0xCF, 0x1F, 0x3F, 0x06, 0x48, 0xBB, 0x33, 0x10, 0x02}},
	 {4, 591,  650, {0x33, 0xF4, 0x15, 0x14, 0x44, 0x12, 0xB4, 0x08, 0x30, 0x4F, 0x4E, 0x10, 0x02}},
	 {3, 448,  492, {0x33, 0xF4, 0x25, 0x0F, 0x52, 0x0D, 0xC2, 0x09, 0x28, 0xC0, 0x69, 0x10, 0x02}},
      },
      /* 20 fps */
      {
	 {0, },
	 {9, 958,  782, {0x32, 0xF4, 0x0D, 0x18, 0x6A, 0x16, 0xDA, 0x0B, 0x58, 0xBE, 0x33, 0xD0, 0x02}},
	 {5, 703,  574, {0x32, 0xF4, 0x1D, 0x11, 0xE7, 0x10, 0x57, 0x0B, 0x40, 0xBF, 0x42, 0xD0, 0x02}},
	 {3, 446,  364, {0x32, 0xF4, 0x3D, 0x0B, 0x5C, 0x09, 0xCC, 0x0E, 0x30, 0xBE, 0x69, 0xD0, 0x02}},
      },
      /* 25 fps */
      {
	 {0, },
	 {9, 958,  654, {0x31, 0xF4, 0x15, 0x14, 0x66, 0x12, 0xD6, 0x0B, 0x50, 0xBE, 0x33, 0x90, 0x02}},
	 {6, 776,  530, {0x31, 0xF4, 0x25, 0x10, 0x8C, 0x0E, 0xFC, 0x0C, 0x48, 0x08, 0x3F, 0x90, 0x02}},
	 {4, 592,  404, {0x31, 0xF4, 0x35, 0x0C, 0x96, 0x0B, 0x06, 0x0B, 0x38, 0x50, 0x4E, 0x90, 0x02}},
      },
      /* 30 fps */
      {
	 {0, },
	 {9, 957,  526, {0x30, 0xF4, 0x25, 0x10, 0x68, 0x0E, 0xD8, 0x0D, 0x58, 0xBD, 0x33, 0x60, 0x02}},
	 {6, 775,  426, {0x30, 0xF4, 0x35, 0x0D, 0x48, 0x0B, 0xB8, 0x0F, 0x50, 0x07, 0x3F, 0x60, 0x02}},
	 {4, 590,  324, {0x30, 0x7A, 0x4B, 0x0A, 0x1C, 0x08, 0xB4, 0x0E, 0x40, 0x4E, 0x52, 0x60, 0x02}},
      },
   },
   /* CIF */
   {
      /* 5 fps */
      {
	 {6, 771,    0, {0x15, 0xF4, 0x04, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x3F, 0x80, 0x02}},
	 {4, 465, 1278, {0x15, 0xF4, 0x05, 0x27, 0xEE, 0x26, 0x36, 0x03, 0x18, 0xD1, 0x65, 0x80, 0x02}},
	 {2, 291,  800, {0x15, 0xF4, 0x15, 0x18, 0xF4, 0x17, 0x3C, 0x05, 0x18, 0x23, 0xA1, 0x80, 0x02}},
	 {1, 193,  528, {0x15, 0xF4, 0x2D, 0x10, 0x7E, 0x0E, 0xC6, 0x0A, 0x18, 0xC1, 0xF4, 0x80, 0x02}},
      },
      /* 10 fps */
      {
	 {0, },
	 {9, 932, 1278, {0x14, 0xF4, 0x05, 0x27, 0xEE, 0x26, 0x36, 0x04, 0x30, 0xA4, 0x33, 0x10, 0x02}},
	 {4, 591,  812, {0x14, 0xF4, 0x15, 0x19, 0x56, 0x17, 0x9E, 0x06, 0x28, 0x4F, 0x4E, 0x10, 0x02}},
	 {2, 291,  400, {0x14, 0xF4, 0x3D, 0x0C, 0x7A, 0x0A, 0xC2, 0x0E, 0x28, 0x23, 0xA1, 0x10, 0x02}},
      },
      /* 15 fps */
      {
	 {0, },
	 {9, 956,  876, {0x13, 0xF4, 0x0D, 0x1B, 0x58, 0x19, 0xA0, 0x05, 0x38, 0xBC, 0x33, 0x60, 0x02}},
	 {5, 703,  644, {0x13, 0xF4, 0x1D, 0x14, 0x1C, 0x12, 0x64, 0x08, 0x38, 0xBF, 0x42, 0x60, 0x02}},
	 {3, 448,  410, {0x13, 0xF4, 0x3D, 0x0C, 0xC4, 0x0B, 0x0C, 0x0E, 0x38, 0xC0, 0x69, 0x60, 0x02}},
      },
      /* 20 fps */
      {
	 {0, },
	 {9, 956,  650, {0x12, 0xF4, 0x1D, 0x14, 0x4A, 0x12, 0x92, 0x09, 0x48, 0xBC, 0x33, 0x10, 0x03}},
	 {6, 776,  528, {0x12, 0xF4, 0x2D, 0x10, 0x7E, 0x0E, 0xC6, 0x0A, 0x40, 0x08, 0x3F, 0x10, 0x03}},
	 {4, 591,  402, {0x12, 0xF4, 0x3D, 0x0C, 0x8F, 0x0A, 0xD7, 0x0E, 0x40, 0x4F, 0x4E, 0x10, 0x03}},
      },
      /* 25 fps */
      {
	 {0, },
	 {9, 956,  544, {0x11, 0xF4, 0x25, 0x10, 0xF4, 0x0F, 0x3C, 0x0A, 0x48, 0xBC, 0x33, 0xC0, 0x02}},
	 {7, 840,  478, {0x11, 0xF4, 0x2D, 0x0E, 0xEB, 0x0D, 0x33, 0x0B, 0x48, 0x48, 0x3B, 0xC0, 0x02}},
	 {5, 703,  400, {0x11, 0xF4, 0x3D, 0x0C, 0x7A, 0x0A, 0xC2, 0x0E, 0x48, 0xBF, 0x42, 0xC0, 0x02}},
      },
      /* 30 fps */
      {
	 {0, },
	 {9, 956,  438, {0x10, 0xF4, 0x35, 0x0D, 0xAC, 0x0B, 0xF4, 0x0D, 0x50, 0xBC, 0x33, 0x10, 0x02}},
	 {7, 838,  384, {0x10, 0xF4, 0x45, 0x0B, 0xFD, 0x0A, 0x45, 0x0F, 0x50, 0x46, 0x3B, 0x10, 0x02}},
	 {6, 773,  354, {0x10, 0x7A, 0x4B, 0x0B, 0x0C, 0x09, 0x80, 0x10, 0x50, 0x05, 0x3F, 0x10, 0x02}},
      },
   },
   /* VGA */
   {
      /* 5 fps */
      {
	 {0, },
	 {6, 773, 1272, {0x1D, 0xF4, 0x15, 0x27, 0xB6, 0x24, 0x96, 0x02, 0x30, 0x05, 0x3F, 0x10, 0x02}},
	 {4, 592,  976, {0x1D, 0xF4, 0x25, 0x1E, 0x78, 0x1B, 0x58, 0x03, 0x30, 0x50, 0x4E, 0x10, 0x02}},
	 {3, 448,  738, {0x1D, 0xF4, 0x3D, 0x17, 0x0C, 0x13, 0xEC, 0x04, 0x30, 0xC0, 0x69, 0x10, 0x02}},
      },
      /* 10 fps */
      {
	 {0, },
	 {9, 956,  788, {0x1C, 0xF4, 0x35, 0x18, 0x9C, 0x15, 0x7C, 0x03, 0x48, 0xBC, 0x33, 0x10, 0x02}},
	 {6, 776,  640, {0x1C, 0x7A, 0x53, 0x13, 0xFC, 0x11, 0x2C, 0x04, 0x48, 0x08, 0x3F, 0x10, 0x02}},
	 {4, 592,  488, {0x1C, 0x7A, 0x6B, 0x0F, 0x3C, 0x0C, 0x6C, 0x06, 0x48, 0x50, 0x4E, 0x10, 0x02}},
      },
      /* 15 fps */
      {
	 {0, },
	 {9, 957,  526, {0x1B, 0x7A, 0x63, 0x10, 0x68, 0x0D, 0x98, 0x06, 0x58, 0xBD, 0x33, 0x80, 0x02}},
	 {9, 957,  526, {0x1B, 0x7A, 0x63, 0x10, 0x68, 0x0D, 0x98, 0x06, 0x58, 0xBD, 0x33, 0x80, 0x02}},
	 {8, 895,  492, {0x1B, 0x7A, 0x6B, 0x0F, 0x5D, 0x0C, 0x8D, 0x06, 0x58, 0x7F, 0x37, 0x80, 0x02}},
      },
      /* 20 fps */
      {
	 {0, },
	 {0, },
	 {0, },
	 {0, },
      },
      /* 25 fps */
      {
	 {0, },
	 {0, },
	 {0, },
	 {0, },
      },
      /* 30 fps */
      {
	 {0, },
	 {0, },
	 {0, },
	 {0, },
      },
   },
};

