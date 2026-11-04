/*
 * Copyright (c) 2002, Thomas Kurschel
 * Copyright 2004-2016 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Thomas Kurschel
 *		Clemens Zeidler, <haiku@clemens-zeidler.de>
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The Haiku copyright and the MIT License above still
 * cover the original work. A build that ships both follows GPL-3.0-or-later,
 * and the MIT notices stay on the Haiku portions.
 */



#include "driver.h"
#include "device.h"
#include "lock.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <AGP.h>
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <SupportDefs.h>


#define TRACE_DRIVER
#ifdef TRACE_DRIVER
#	define TRACE(x...) dprintf("radeon_hd: " x)
#else
#	define TRACE(x...) ;
#endif

#define ERROR(x...) dprintf("radeon_hd: " x)

#define MAX_CARDS 1


// ATI / AMD cards starting at the Radeon X700 have an AtomBIOS

// list of supported devices
const struct supported_device {
	uint32		pciID;
	uint8		dceMajor;	// DCE register generation. 0 when the engine is not DCE.
	uint8		dceMinor;
	uint16		chipsetID;
	uint32		chipsetFlags;
	const char*	deviceName;
	// Older rows leave these at the defaults and keep dceMajor/dceMinor.
	uint8		displayClass = RHD_DISPLAY_DCE;
	uint8		displayMajor = 0;
	uint8		displayMinor = 0;
	uint8		displayStepping = 0;
} kSupportedDevices[] = {
	// Marketing Names: Radeon X?00
	// Intorduced: 2004
	// Codename: Loki
	// R420 Series  (Radeon) DCE 0.0 (*very* early AtomBIOS)

	// Marketing Names: Radeon X1?00
	// Introduced: 2005
	// Codename: Fudo
	#if 0
	{0x791e, 1, 0, RADEON_RS690, CHIP_IGP, "Radeon X1200"},
	{0x791f, 1, 0, RADEON_RS690, CHIP_IGP, "Radeon X1200"},
	{0x793f, 1, 0, RADEON_RS600, CHIP_IGP, "Radeon X1200"},
	{0x7941, 1, 0, RADEON_RS600, CHIP_IGP, "Radeon X1200"},
	{0x7942, 1, 0, RADEON_RS600, CHIP_IGP, "Radeon X1250"},
	{0x796c, 1, 0, RADEON_RS740, CHIP_IGP, "Radeon RS740"},
	{0x796d, 1, 0, RADEON_RS740, CHIP_IGP, "Radeon RS740"},
	{0x796e, 1, 0, RADEON_RS740, CHIP_IGP, "Radeon 2100"},
	{0x796f, 1, 0, RADEON_RS740, CHIP_IGP, "Radeon RS740"},
	{0x7140, 1, 0, RADEON_RV515, CHIP_STD, "Radeon X1600"},
	{0x7100, 1, 0, RADEON_R520,  CHIP_STD, "Radeon X1800"},
	{0x7104, 1, 0, RADEON_R520,  CHIP_STD, "FireGL v7200"},
	{0x7109, 1, 0, RADEON_R520,  CHIP_STD, "Radeon X1800"},
	{0x710a, 1, 0, RADEON_R520,  CHIP_STD, "Radeon X1800"},
	{0x710b, 1, 0, RADEON_R520,  CHIP_STD, "Radeon X1800"},
	{0x710c, 1, 0, RADEON_R520,  CHIP_STD, "Radeon X1800"},
	{0x7120, 1, 0, RADEON_R520,  CHIP_STD, "Radeon X1800"},
	{0x7129, 1, 0, RADEON_R520,  CHIP_STD, "Radeon X1800"},
	#endif

	// Marketing Names: Radeon HD 24xx - HD 42xx
	// Introduced: 2006
	// Codename: Pele
	// Process: 55 nm
	{0x94c7, 2, 0, RADEON_RV610, CHIP_STD, "Radeon HD 2350"},
	{0x94c1, 2, 0, RADEON_RV610, CHIP_IGP, "Radeon HD 2400"},
	{0x94c3, 2, 0, RADEON_RV610, CHIP_STD, "Radeon HD 2400"},
	{0x94c9, 2, 0, RADEON_RV610, CHIP_IGP, "Mobility Radeon HD 2400"},
	{0x94cc, 2, 0, RADEON_RV610, CHIP_STD, "Radeon HD 2400"},
	{0x9519, 2, 0, RADEON_RV630, CHIP_STD, "AMD FireStream 9170"},
	{0x9586, 2, 0, RADEON_RV630, CHIP_STD, "Radeon HD 2600"},
	{0x9588, 2, 0, RADEON_RV630, CHIP_STD, "Radeon HD 2600"},
	{0x958a, 2, 0, RADEON_RV630, CHIP_STD, "Radeon HD 2600 X2"},
	//	Radeon 2700		- RV630
	{0x9400, 2, 0, RADEON_R600,  CHIP_STD, "Radeon HD 2900"},
	{0x9401, 2, 0, RADEON_R600,  CHIP_STD, "Radeon HD 2900"},
	{0x9402, 2, 0, RADEON_R600,  CHIP_STD, "Radeon HD 2900"},
	{0x9403, 2, 0, RADEON_R600,  CHIP_STD, "Radeon HD 2900 Pro"},
	{0x9405, 2, 0, RADEON_R600,  CHIP_STD, "Radeon HD 2900"},
	{0x940a, 2, 0, RADEON_R600,  CHIP_STD, "Radeon FireGL V8650"},
	{0x940b, 2, 0, RADEON_R600,  CHIP_STD, "Radeon FireGL V8600"},
	{0x940f, 2, 0, RADEON_R600,  CHIP_STD, "Radeon FireGL V7600"},
	{0x9616, 2, 0, RADEON_RV610, CHIP_IGP, "Radeon HD 3000"},
	{0x9611, 3, 0, RADEON_RV620, CHIP_IGP, "Radeon HD 3100"},
	{0x9613, 3, 0, RADEON_RV620, CHIP_IGP, "Radeon HD 3100"},
	{0x9610, 2, 0, RADEON_RV610, CHIP_IGP, "Radeon HD 3200"},
	{0x9612, 2, 0, RADEON_RV610, CHIP_IGP, "Radeon HD 3200"},
	{0x9615, 2, 0, RADEON_RV610, CHIP_IGP, "Radeon HD 3200"},
	{0x9614, 2, 0, RADEON_RV610, CHIP_IGP, "Radeon HD 3300"},
	//  Radeon 3430		- RV620
	{0x95c2, 3, 0, RADEON_RV620, CHIP_STD, "Radeon HD 3430"},
	{0x95c5, 3, 0, RADEON_RV620, CHIP_STD, "Radeon HD 3450"},
	{0x95c6, 3, 0, RADEON_RV620, CHIP_STD, "Radeon HD 3450"},
	{0x95c7, 3, 0, RADEON_RV620, CHIP_STD, "Radeon HD 3450"},
	{0x95c9, 3, 0, RADEON_RV620, CHIP_STD, "Radeon HD 3450"},
	{0x95c4, 3, 0, RADEON_RV620, CHIP_STD, "Radeon HD 3470"},
	{0x95c0, 3, 0, RADEON_RV620, CHIP_STD, "Radeon HD 3550"},
	{0x9581, 2, 0, RADEON_RV630, CHIP_STD, "Radeon HD 3600"},
	{0x9583, 2, 0, RADEON_RV630, CHIP_STD, "Radeon HD 3600"},
	{0x9598, 2, 0, RADEON_RV630, CHIP_STD, "Radeon HD 3600"},
	{0x9591, 3, 0, RADEON_RV635, CHIP_STD, "Radeon HD 3600"},
	{0x9589, 2, 0, RADEON_RV630, CHIP_STD, "Radeon HD 3610"},
	//  Radeon 3650		- RV635
	//  Radeon 3670		- RV635
	{0x9507, 2, 0, RADEON_RV670, CHIP_STD, "Radeon HD 3830"},
	{0x9505, 2, 0, RADEON_RV670, CHIP_STD, "Radeon HD 3850"},
	{0x9513, 2, 0, RADEON_RV670, CHIP_STD, "Radeon HD 3850 X2"},
	{0x9515, 2, 0, RADEON_RV670, CHIP_STD, "Radeon HD 3850"},
	{0x9501, 2, 0, RADEON_RV670, CHIP_STD, "Radeon HD 3870"},
	{0x950F, 2, 0, RADEON_RV670, CHIP_STD | CHIP_X2, "Radeon HD 3870 X2"},
	{0x9710, 3, 0, RADEON_RV620, CHIP_IGP, "Radeon HD 4200"},
	{0x9715, 3, 0, RADEON_RV620, CHIP_IGP, "Radeon HD 4250"},
	{0x9712, 3, 0, RADEON_RV620, CHIP_IGP, "Radeon HD 4270"},
	{0x9714, 3, 0, RADEON_RV620, CHIP_IGP, "Radeon HD 4290"},

	// Marketing Names: Radeon HD 4330 - HD 4890, HD 51xx, HD 5xxV
	// Introduced: 2008
	// Codename: Wekiva
	// Process: 55 nm
	//	Radeon 4330		- RV710
	{0x954f, 3, 2, RADEON_RV710, CHIP_IGP, "Radeon HD 4300"},
	{0x9552, 3, 2, RADEON_RV710, CHIP_IGP, "Radeon HD 4300"},
	{0x9553, 3, 2, RADEON_RV710, CHIP_IGP, "Radeon HD 4500"},
	{0x9555, 3, 2, RADEON_RV710, CHIP_STD, "Radeon HD 4350"},
	{0x9540, 3, 2, RADEON_RV710, CHIP_STD, "Radeon HD 4550"},
	{0x9452, 3, 2, RADEON_RV730, CHIP_STD, "AMD FireStream 9250"},
	{0x9480, 3, 2, RADEON_RV730, CHIP_STD, "Radeon HD 4650"},
	{0x9488, 3, 2, RADEON_RV730, CHIP_MOBILE, "Radeon HD 4650"},
	{0x9498, 3, 2, RADEON_RV730, CHIP_STD, "Radeon HD 4650"},
	{0x94b4, 3, 2, RADEON_RV740, CHIP_STD, "Radeon HD 4700"},
	{0x9490, 3, 2, RADEON_RV730, CHIP_STD, "Radeon HD 4710"},
	{0x94b3, 3, 2, RADEON_RV740, CHIP_STD, "Radeon HD 4770"},
	{0x94b5, 3, 2, RADEON_RV740, CHIP_STD, "Radeon HD 4770"},
	{0x9450, 3, 1, RADEON_RV770, CHIP_STD, "AMD FireStream 9270"},
	{0x944a, 3, 1, RADEON_RV770, CHIP_MOBILE, "Radeon HD 4850"},
	{0x944e, 3, 1, RADEON_RV770, CHIP_STD, "Radeon HD 4810"},
	{0x944c, 3, 1, RADEON_RV770, CHIP_STD, "Radeon HD 4830"},
	{0x9442, 3, 1, RADEON_RV770, CHIP_STD, "Radeon HD 4850"},
	{0x9443, 3, 1, RADEON_RV770, CHIP_STD | CHIP_X2, "Radeon HD 4850 X2"},
	{0x94a1, 3, 1, RADEON_RV770, CHIP_IGP, "Radeon HD 4860"},
	{0x9440, 3, 1, RADEON_RV770, CHIP_STD, "Radeon HD 4870"},
	{0x9441, 3, 1, RADEON_RV770, CHIP_STD | CHIP_X2, "Radeon HD 4870 X2"},
	{0x9460, 3, 1, RADEON_RV770, CHIP_STD, "Radeon HD 4890"},

	// From here on AMD no longer used numeric identifiers
	// Marketing names can collide for different generations of cards
	// as such we should ignore them at all costs (besides the card name)

	// Introduced: 2009
	// Codename: Evergreen
	// Process: 40 nm
	//  Cedar
	{0x68e1, 4, 0, RADEON_CEDAR, CHIP_STD, "Radeon HD 5430"},
	{0x68f9, 4, 0, RADEON_CEDAR, CHIP_STD, "Radeon HD 5450"},
	{0x68e0, 4, 0, RADEON_CEDAR, CHIP_IGP, "Radeon HD 5470"},
	{0x68e4, 4, 0, RADEON_CEDAR, CHIP_MOBILE, "Radeon HD 6370"},
	//  Redwood
	{0x68da, 4, 0, RADEON_REDWOOD, CHIP_STD, "Radeon HD 5500"},
	{0x68d9, 4, 0, RADEON_REDWOOD, CHIP_STD, "Radeon HD 5570"},
	{0x675f, 4, 0, RADEON_REDWOOD, CHIP_STD, "Radeon HD 5570"},
	{0x68b9, 4, 0, RADEON_REDWOOD, CHIP_STD, "Radeon HD 5600"},
	{0x68c1, 4, 0, RADEON_REDWOOD, CHIP_STD, "Radeon HD 5650"},
	{0x68c8, 4, 0, RADEON_REDWOOD, CHIP_STD, "FirePro V4800"},
	{0x68d8, 4, 0, RADEON_REDWOOD, CHIP_STD, "Radeon HD 5670"},
	//  Juniper
	{0x68be, 4, 0, RADEON_JUNIPER, CHIP_STD, "Radeon HD 5700"},
	{0x68b8, 4, 0, RADEON_JUNIPER, CHIP_STD, "Radeon HD 5770"},
	//  Juniper LE / XT (67X0 is rebranded 57X0 + tweaks)
#if 0
	// Not working: #8154
	{0x68bf, 4, 0, RADEON_JUNIPER, CHIP_STD, "Radeon HD 6750"},
#endif
	{0x68ba, 4, 0, RADEON_JUNIPER, CHIP_STD, "Radeon HD 6770"},
	//  Cypress
	{0x689e, 4, 0, RADEON_CYPRESS, CHIP_STD, "Radeon HD 5800"},
	{0x6899, 4, 0, RADEON_CYPRESS, CHIP_STD, "Radeon HD 5850"},
	{0x6898, 4, 0, RADEON_CYPRESS, CHIP_STD, "Radeon HD 5870"},
	//  Hemlock
	{0x689c, 4, 0, RADEON_HEMLOCK, CHIP_STD | CHIP_X2, "Radeon HD 5900 X2"},
	{0x689d, 4, 0, RADEON_HEMLOCK, CHIP_STD | CHIP_X2, "Radeon HD 5900 X2"},
	// Fusion APUS
	//  Palm
	{0x9804, 4, 1, RADEON_PALM, CHIP_APU, "Radeon HD 6250"},
	{0x9805, 4, 1, RADEON_PALM, CHIP_APU, "Radeon HD 6290"},
	{0x9807, 4, 1, RADEON_PALM, CHIP_APU, "Radeon HD 6290"},
	{0x9802, 4, 1, RADEON_PALM, CHIP_APU, "Radeon HD 6310"},
	{0x9803, 4, 1, RADEON_PALM, CHIP_APU, "Radeon HD 6310"},
	{0x9806, 4, 1, RADEON_PALM, CHIP_APU, "Radeon HD 6320"},
	{0x9808, 4, 1, RADEON_PALM, CHIP_APU, "Radeon HD 7340"},
	//  Sumo (no VGA / LVDS!, only DP)
	{0x9640, 4, 1, RADEON_SUMO, CHIP_APU, "Radeon HD 6550D"},
	{0x9641, 4, 1, RADEON_SUMO, CHIP_APU, "Radeon HD Sumo"},
#if 0
	// Not working: #10335, #12026
	{0x9647, 4, 1, RADEON_SUMO, CHIP_APU, "Radeon HD 6520G"},
	{0x9648, 4, 1, RADEON_SUMO, CHIP_APU, "Radeon HD 6480G"},
#endif
	{0x964a, 4, 1, RADEON_SUMO, CHIP_APU, "Radeon HD 6530D"},
	{0x964e, 4, 1, RADEON_SUMO, CHIP_APU, "Radeon HD Sumo"},
	{0x964f, 4, 1, RADEON_SUMO, CHIP_APU, "Radeon HD Sumo"},
	//  Sumo2 (no VGA / LVDS!, only DP)
	{0x9642, 4, 1, RADEON_SUMO2, CHIP_APU, "Radeon HD 6370D"},
	{0x9643, 4, 1, RADEON_SUMO2, CHIP_APU, "Radeon HD Sumo2"},
	{0x9644, 4, 1, RADEON_SUMO2, CHIP_APU, "Radeon HD 6410D"},
	{0x9645, 4, 1, RADEON_SUMO2, CHIP_APU, "Radeon HD Sumo2"},

	// Introduced: 2010
	// Codename: Nothern Islands
	// Process: 40 nm
	//  Caicos
#if 0
	// Not working: #12313
	{0x6760, 5, 0, RADEON_CAICOS, CHIP_MOBILE, "Radeon HD 6470"},
#endif
	{0x6761, 5, 0, RADEON_CAICOS, CHIP_MOBILE, "Radeon HD 6430"},
	{0x6762, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD Caicos"},
	{0x6763, 5, 0, RADEON_CAICOS, CHIP_DISCREET, "Radeon HD E6460"},
	{0x6764, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD Caicos"},
	{0x6765, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD Caicos"},
	{0x6766, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD Caicos"},
	{0x6767, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD Caicos"},
	{0x6768, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD Caicos"},
	{0x6770, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD 6400"},
	{0x6778, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD 7470/8470"},
	{0x6779, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD 6450/7450/8450"},
	{0x68fa, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD 7350"},
	{0x677b, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD 7400"},
	{0x6772, 5, 0, RADEON_CAICOS, CHIP_APU, "Radeon HD 7400A"},
	{0x6771, 5, 0, RADEON_CAICOS, CHIP_STD, "Radeon HD 8490"},
	//  Turks
	{0x6740, 5, 0, RADEON_TURKS, CHIP_MOBILE, "Radeon HD 6770M"},
	{0x6741, 5, 0, RADEON_TURKS, CHIP_MOBILE, "Radeon HD 6650M"},
	{0x6742, 5, 0, RADEON_TURKS, CHIP_MOBILE, "Radeon HD 6625M"},
	{0x6743, 5, 0, RADEON_TURKS, CHIP_DISCREET, "Radeon HD E6760"},
	{0x6744, 5, 0, RADEON_TURKS, CHIP_MOBILE, "Radeon HD Turks"},
	{0x6745, 5, 0, RADEON_TURKS, CHIP_MOBILE, "Radeon HD Turks"},
	{0x6746, 5, 0, RADEON_TURKS, CHIP_STD, "FirePro V???? (turks)"},
	{0x6747, 5, 0, RADEON_TURKS, CHIP_STD, "FirePro V???? (turks)"},
	{0x6748, 5, 0, RADEON_TURKS, CHIP_STD, "FirePro V???? (turks)"},
	{0x6749, 5, 0, RADEON_TURKS, CHIP_STD, "FirePro V4900"},
	{0x6750, 5, 0, RADEON_TURKS, CHIP_STD, "Radeon HD 6500"},
	{0x6758, 5, 0, RADEON_TURKS, CHIP_STD, "Radeon HD 6670"},
	{0x6759, 5, 0, RADEON_TURKS, CHIP_STD, "Radeon HD 6570/7570"},
	{0x675d, 5, 0, RADEON_TURKS, CHIP_STD, "Radeon HD 7570"},
	{0x6850, 6, 0, RADEON_TURKS, CHIP_MOBILE, "Radeon HD 7570"},
#if 0
	// Not working: #12026
	{0x6840, 6, 0, RADEON_TURKS, CHIP_MOBILE, "Radeon HD 7670"},
#endif
	//  Barts
	{0x673e, 5, 0, RADEON_BARTS, CHIP_STD, "Radeon HD 6790"},
#if 0
	// Not working: #8765
	{0x6739, 5, 0, RADEON_BARTS, CHIP_STD, "Radeon HD 6850"},
#endif
	{0x6738, 5, 0, RADEON_BARTS, CHIP_STD, "Radeon HD 6870"},
	//  Cayman
	{0x6700, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD Cayman"},
	{0x6701, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD Cayman"},
	{0x6702, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD Cayman"},
	{0x6703, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD Cayman"},
	{0x6704, 5, 0, RADEON_CAYMAN, CHIP_STD, "FirePro V7900"},
	{0x6705, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD Cayman"},
	{0x6706, 5, 0, RADEON_CAYMAN, CHIP_STD, "FirePro V???? (cayman)"},
	{0x6707, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD Cayman"},
	{0x6708, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD Cayman"},
	{0x6709, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD Cayman"},
	{0x6718, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD 6970"},
	{0x6719, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD 6950"},
	{0x671c, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD Cayman"},
	{0x671f, 5, 0, RADEON_CAYMAN, CHIP_STD, "Radeon HD 6900"},
	//  Antilles (Top, Dual GPU)
	{0x671d, 5, 0, RADEON_ANTILLES, CHIP_STD, "Radeon HD 6990"},


	// Introduced: Late 2011
	// Codename: Southern Islands
	// Process: 28 nm
	//  Cape Verde
	{0x6820, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6821, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6823, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6824, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6825, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6826, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6827, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6828, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6829, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x682a, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x682b, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x682c, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "FirePro W4100"},
	{0x682d, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x682f, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6830, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6831, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6835, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon R7 Series"},
	{0x6837, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6838, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x6839, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x683b, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde"},
	{0x683d, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD 7770"},
	{0x683f, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD 7750"},
	//  Pitcairn
	{0x6800, 6, 0, RADEON_PITCAIRN, CHIP_MOBILE, "Radeon HD 7970"},
	{0x6801, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD Pitcairn"},
	{0x6802, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD Pitcairn"},
	{0x6806, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD Pitcairn"},
	{0x6808, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD Pitcairn"},
	{0x6809, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD Pitcairn"},
	{0x6810, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD R9 270X"},
	{0x6811, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD R9 270"},
	{0x6816, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD Pitcairn"},
	{0x6817, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD Pitcairn"},
	{0x6818, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD 7870"},
	{0x6819, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD 7800"},
	{0x684c, 6, 0, RADEON_PITCAIRN, CHIP_STD, "Radeon HD Pitcairn"},
	//  Tahiti
	{0x6780, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti"},
	{0x6784, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti"},
	{0x6788, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti"},
	{0x678a, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti"},
	{0x6790, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti"},
	{0x679e, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti"},
	{0x679f, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti"},
	{0x679a, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD 7950"},
	{0x6798, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD 7970"},
	{0x6799, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD 7990"},
	//  Aruba   DCE 6.1 Trinity / Richland
	{0x9900, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7660"},
	{0x9901, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7660"},
	{0x9903, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7640"},
	{0x9904, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7560"},
	{0x9905, 6, 1, RADEON_ARUBA, CHIP_APU, "FirePro A300"},
	{0x9906, 6, 1, RADEON_ARUBA, CHIP_APU, "FirePro A300"},
	{0x9907, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7560"},
	{0x9908, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7620"},
	{0x9909, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7500"},
	{0x990a, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7500"},
	{0x990b, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8650"},
	{0x990d, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8550"},
	{0x990f, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8610"},
	{0x9910, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7660"},
	{0x9913, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7640"},
	{0x9917, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7620"},
	{0x9918, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7600"},
	{0x9919, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7500"},
	{0x9990, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7920"},
	{0x9991, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7540"},
	{0x9992, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7420"},
#if 0
	// Known not to work: https://dev.haiku-os.org/ticket/10606
	{0x9993, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7480"},
#endif
	{0x9994, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7400"},
	{0x9995, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8450"},
	{0x9996, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8470"},
	{0x9997, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8350"},
#if 0
	// Known not to work: reported by ttcoder & Dane
	{0x9998, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8730"},
#endif
	{0x999a, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8410"},
	{0x999b, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8310"},
	{0x999c, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8650"},
	{0x999d, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 8550"},
#if 0
	// Not working: #12026
	{0x99a0, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7520"},
#endif
	{0x99a2, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7420"},
	{0x99a4, 6, 1, RADEON_ARUBA, CHIP_APU, "Radeon HD 7400"},
	//  Oland   DCE 6,4
	{0x990c, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon HD 8670"},
	{0x990e, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon HD 8570"},
	{0x6600, 6, 4, RADEON_OLAND, CHIP_MOBILE, "Radeon HD 8750M"},
	{0x6601, 6, 4, RADEON_OLAND, CHIP_MOBILE, "Radeon HD 8730M"},
	{0x6602, 6, 4, RADEON_OLAND, CHIP_MOBILE, "Radeon HD Oland"},
	{0x6603, 6, 4, RADEON_OLAND, CHIP_MOBILE, "Radeon HD Oland"},
	{0x6604, 6, 4, RADEON_OLAND, CHIP_MOBILE, "Radeon R7 M270"},
	{0x6605, 6, 4, RADEON_OLAND, CHIP_MOBILE, "Radeon R7 M260"},
	{0x6606, 6, 4, RADEON_OLAND, CHIP_MOBILE, "Radeon HD 8790"},
	{0x6607, 6, 4, RADEON_OLAND, CHIP_MOBILE, "Radeon HD 8350"},
	{0x6608, 6, 4, RADEON_OLAND, CHIP_STD, "FirePro W2100"},
	{0x6610, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon R7 250"},
	{0x6611, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon R7 200"},
	{0x6613, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon R7 240"},
	{0x6620, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon R7 240"},
	{0x6621, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon HD Oland"},
	{0x6623, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon HD Oland"},
	{0x6631, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon HD Oland"},
	//  Hainan  NODCE. No display hardware, OpenCL 3D engine.
	//{0x6670, 0, 0, RADEON_HAINAN, CHIP_COM, "Radeon HD Hainan compute"},

	// Introduced: Late 2013
	// Codename: Sea Islands
	// Process: 28 nm
	//  Kaveri DCE 8.1
	{0x1304, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1305, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1306, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1307, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1308, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1309, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R7 Series"},
	{0x130a, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R6 Series"},
	{0x130b, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R4 Series"},
	{0x130c, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R7 Series"},
	{0x130d, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R6 Series"},
	{0x130e, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R5 Series"},
	{0x130f, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R7 Series"},
	{0x1310, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1311, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1312, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1313, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R7 Series"},
	{0x1314, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1315, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R5 Series"},
	{0x1316, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R5 Series"},
	{0x1317, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon HD Kaveri"},
	{0x1318, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R5 Series"},
	{0x131b, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R5 Series"},
	{0x131c, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R7 Series"},
	{0x131d, 8, 1, RADEON_KAVERI, CHIP_STD, "Radeon R6 Series"},
	//  Bonaire DCE 8.2
	{0x6640, 8, 2, RADEON_BONAIRE, CHIP_MOBILE, "Radeon R9 380"},
	{0x6641, 8, 2, RADEON_BONAIRE, CHIP_MOBILE, "Radeon HD 8930M"},
	{0x6646, 8, 2, RADEON_BONAIRE, CHIP_MOBILE, "Radeon R9 380"},
	{0x6647, 8, 2, RADEON_BONAIRE, CHIP_MOBILE, "Radeon R9 380"},
	{0x6649, 8, 2, RADEON_BONAIRE, CHIP_STD, "FirePro W5100"},
	{0x6650, 8, 2, RADEON_BONAIRE, CHIP_STD, "Radeon HD Bonaire"},
	{0x6651, 8, 2, RADEON_BONAIRE, CHIP_STD, "Radeon HD Bonaire"},
	{0x6658, 8, 2, RADEON_BONAIRE, CHIP_STD, "Radeon R7 200"},
	{0x665c, 8, 2, RADEON_BONAIRE, CHIP_STD, "Radeon HD 7790"},
	{0x665d, 8, 2, RADEON_BONAIRE, CHIP_STD, "Radeon R7 200"},
	{0x665f, 8, 2, RADEON_BONAIRE, CHIP_STD, "Radeon R7 300"},
	//  Kabini DCE 8.3
#if 0
	// Not working: #10939
	{0x9830, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8400"},
	{0x9831, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8400"},
#endif
	{0x9832, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8330"},
	{0x9833, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8330"},
	{0x9834, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8210"},
	{0x9835, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8210"},
	{0x9836, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8280"},
	{0x9837, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8280"},
	{0x9838, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8240"},
	{0x9839, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8180"},
	{0x983a, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD Kabini"},
	{0x983b, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD Kabini"},
	{0x983c, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD Kabini"},
	{0x983d, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD 8250"},
	{0x983e, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD Kabini"},
	{0x983f, 8, 3, RADEON_KABINI, CHIP_STD, "Radeon HD Kabini"},
	//  Mullins DCE 8.3
	{0x9850, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD R3"},
	{0x9851, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD R2"},
	{0x9852, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD R2"},
	{0x9853, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD R2"},
	{0x9854, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD R2"},
	{0x9855, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD R3"},
	{0x9856, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD R2"},
	{0x9857, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD R2"},
	{0x9858, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD Mullins"},
	{0x9859, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD Mullins"},
	{0x985a, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD Mullins"},
	{0x985b, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD Mullins"},
	{0x985c, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD Mullins"},
	{0x985d, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD Mullins"},
	{0x985e, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD Mullins"},
	{0x985f, 8, 3, RADEON_MULLINS, CHIP_STD, "Radeon HD Mullins"},
	//  Hawaii DCE 8.5
	{0x67a0, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon FirePro W9100"},
	{0x67a1, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon FirePro W8100"},
	{0x67a2, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon HD Hawaii"},
	{0x67a8, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon HD Hawaii"},
	{0x67a9, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon HD Hawaii"},
	{0x67aa, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon HD Hawaii"},
	{0x67b0, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon R9 290X/390X"},
	{0x67b1, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon R9 290/390"},
	{0x67b8, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon HD Hawaii"},
	{0x67b9, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon R9 295X2"},
	{0x67ba, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon HD Hawaii"},
	{0x67be, 8, 5, RADEON_HAWAII, CHIP_STD, "Radeon HD Hawaii"},

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	// Families from here were inside `#if 0` for Haiku R1/beta5.
	// PCI IDs follow Linux amdgpu_drv.c v6.12, except 0x163f which that
	// file no longer lists and which Linux v5.15 calls CHIP_VANGOGH.
	// Display versions follow Linux vi.c IP blocks and
	// dc_resource.c resource_parse_asic_id (v6.12). dceMajor stays 0
	// on DCN and on engines with no display, so the DCE register paths
	// do not treat those versions as DCE 12 or 13.
	// Topaz: no display. vi.c does not add a DCE block.
	{0x6900, 0, 0, RADEON_TOPAZ, CHIP_STD, "Radeon Topaz", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x6901, 0, 0, RADEON_TOPAZ, CHIP_STD, "Radeon Topaz", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x6902, 0, 0, RADEON_TOPAZ, CHIP_STD, "Radeon Topaz", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x6903, 0, 0, RADEON_TOPAZ, CHIP_STD, "Radeon Topaz", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x6907, 0, 0, RADEON_TOPAZ, CHIP_STD, "Radeon Topaz", RHD_DISPLAY_NONE, 0, 0, 0},
	// Tonga: DCE 10.0 (dce_v10_0).
	{0x6920, 10, 0, RADEON_TONGA, CHIP_STD, "Radeon R9 M395/M395X", RHD_DISPLAY_DCE, 10, 0, 0},
	{0x6921, 10, 0, RADEON_TONGA, CHIP_STD, "Radeon R9 M295/M390X", RHD_DISPLAY_DCE, 10, 0, 0},
	{0x6928, 10, 0, RADEON_TONGA, CHIP_STD, "Radeon Tonga", RHD_DISPLAY_DCE, 10, 0, 0},
	{0x6929, 10, 0, RADEON_TONGA, CHIP_STD, "Radeon FirePro S7150", RHD_DISPLAY_DCE, 10, 0, 0},
	{0x692b, 10, 0, RADEON_TONGA, CHIP_STD, "Radeon FirePro W7100", RHD_DISPLAY_DCE, 10, 0, 0},
	{0x692f, 10, 0, RADEON_TONGA, CHIP_STD, "Radeon FirePro S7150V", RHD_DISPLAY_DCE, 10, 0, 0},
	{0x6930, 10, 0, RADEON_TONGA, CHIP_STD, "Radeon Tonga", RHD_DISPLAY_DCE, 10, 0, 0},
	{0x6938, 10, 0, RADEON_TONGA, CHIP_STD, "Radeon R9 380X/M295X", RHD_DISPLAY_DCE, 10, 0, 0},
	{0x6939, 10, 0, RADEON_TONGA, CHIP_STD, "Radeon R9 285/380", RHD_DISPLAY_DCE, 10, 0, 0},
	// Fiji: DCE 10.1 (dce_v10_1). The DC parser groups it with Tonga as 10.0.
	{0x7300, 10, 1, RADEON_FIJI, CHIP_STD, "Radeon R9 Fury", RHD_DISPLAY_DCE, 10, 1, 0},
	{0x730f, 10, 1, RADEON_FIJI, CHIP_STD, "Radeon Fiji", RHD_DISPLAY_DCE, 10, 1, 0},
	// Carrizo and Stoney: vi.c registers dce_v11_0 for both.
	{0x9870, 11, 0, RADEON_CARRIZO, CHIP_APU, "Radeon Carrizo", RHD_DISPLAY_DCE, 11, 0, 0},
	{0x9874, 11, 0, RADEON_CARRIZO, CHIP_APU, "Radeon Carrizo", RHD_DISPLAY_DCE, 11, 0, 0},
	{0x9875, 11, 0, RADEON_CARRIZO, CHIP_APU, "Radeon Carrizo", RHD_DISPLAY_DCE, 11, 0, 0},
	{0x9876, 11, 0, RADEON_CARRIZO, CHIP_APU, "Radeon Carrizo", RHD_DISPLAY_DCE, 11, 0, 0},
	{0x9877, 11, 0, RADEON_CARRIZO, CHIP_APU, "Radeon Carrizo", RHD_DISPLAY_DCE, 11, 0, 0},
	{0x98e4, 11, 0, RADEON_STONEY, CHIP_APU, "Radeon Stoney Ridge", RHD_DISPLAY_DCE, 11, 0, 0},
	// Polaris 10/11/12 and Vega M: dce_v11_2. Vega M is DCE 11.22 in the DC enum.
	{0x67c0, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Pro WX 7100 Mobile", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67c1, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Polaris 10", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67c2, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Pro V7300X", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67c4, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Pro WX 7100", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67c7, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Pro WX 5100", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67c8, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Polaris 10", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67c9, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Polaris 10", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67ca, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Polaris 10", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67cc, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Polaris 10", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67cf, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Polaris 10", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67d0, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Pro V7300X", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67df, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon RX 470/480/570/580/590", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67e0, 11, 2, RADEON_POLARIS11, CHIP_STD, "Radeon Pro WX 4170", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67e3, 11, 2, RADEON_POLARIS11, CHIP_STD, "Radeon Pro WX 4100", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67e8, 11, 2, RADEON_POLARIS11, CHIP_STD, "Radeon Pro WX 4130", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67eb, 11, 2, RADEON_POLARIS11, CHIP_STD, "Radeon Pro V5300X", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67ef, 11, 2, RADEON_POLARIS11, CHIP_STD, "Radeon RX 460", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67ff, 11, 2, RADEON_POLARIS11, CHIP_STD, "Radeon RX 560", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67e1, 11, 2, RADEON_POLARIS11, CHIP_STD, "Radeon Polaris 11", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67e7, 11, 2, RADEON_POLARIS11, CHIP_STD, "Radeon Polaris 11", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x67e9, 11, 2, RADEON_POLARIS11, CHIP_STD, "Radeon Polaris 11", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x6980, 11, 2, RADEON_POLARIS12, CHIP_STD, "Radeon Polaris 12", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x6981, 11, 2, RADEON_POLARIS12, CHIP_STD, "Radeon Pro WX 3200", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x6985, 11, 2, RADEON_POLARIS12, CHIP_STD, "Radeon Pro WX 3100", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x6986, 11, 2, RADEON_POLARIS12, CHIP_STD, "Radeon Polaris 12", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x6987, 11, 2, RADEON_POLARIS12, CHIP_STD, "Radeon 540X/550X/RX 640", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x6995, 11, 2, RADEON_POLARIS12, CHIP_STD, "Radeon Pro WX 2100", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x6997, 11, 2, RADEON_POLARIS12, CHIP_STD, "Radeon Polaris 12", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x699f, 11, 2, RADEON_POLARIS12, CHIP_STD, "Radeon 540/540X/RX 540X", RHD_DISPLAY_DCE, 11, 2, 0},
	{0x694c, 11, 2, RADEON_VEGAM, CHIP_MOBILE, "Radeon RX Vega M GH", RHD_DISPLAY_DCE, 11, 2, 2},
	{0x694e, 11, 2, RADEON_VEGAM, CHIP_MOBILE, "Radeon RX Vega M GL", RHD_DISPLAY_DCE, 11, 2, 2},
	{0x694f, 11, 2, RADEON_VEGAM, CHIP_MOBILE, "Radeon Pro WX Vega M GL", RHD_DISPLAY_DCE, 11, 2, 2},
	// Vega 10 and 12: DCE 12.0. Vega 20: DCE 12.1.
	{0x6860, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Instinct MI25", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x6861, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Pro WX 9100", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x6862, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Pro SSG", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x6863, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Vega Frontier", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x6864, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Pro V340", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x6867, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Pro Vega 56", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x6868, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Pro WX 8100/8200", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x6869, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Vega 10", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x686a, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Vega 10", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x686b, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Vega 10", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x686c, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Instinct MI25 MxGPU", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x686d, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Vega 10", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x686e, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Vega 10", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x686f, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Vega 10", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x687f, 12, 0, RADEON_VEGA10, CHIP_STD, "Radeon Vega 56/64", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x69a0, 12, 0, RADEON_VEGA12, CHIP_STD, "Radeon Vega 12", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x69a1, 12, 0, RADEON_VEGA12, CHIP_STD, "Radeon Vega 12", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x69a2, 12, 0, RADEON_VEGA12, CHIP_STD, "Radeon Vega 12", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x69a3, 12, 0, RADEON_VEGA12, CHIP_STD, "Radeon Vega 12", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x69af, 12, 0, RADEON_VEGA12, CHIP_STD, "Radeon Pro Vega 12", RHD_DISPLAY_DCE, 12, 0, 0},
	{0x66a0, 12, 1, RADEON_VEGA20, CHIP_STD, "Radeon Vega 20", RHD_DISPLAY_DCE, 12, 1, 0},
	{0x66a1, 12, 1, RADEON_VEGA20, CHIP_STD, "Radeon Vega 20", RHD_DISPLAY_DCE, 12, 1, 0},
	{0x66a2, 12, 1, RADEON_VEGA20, CHIP_STD, "Radeon Vega 20", RHD_DISPLAY_DCE, 12, 1, 0},
	{0x66a3, 12, 1, RADEON_VEGA20, CHIP_STD, "Radeon Vega 20", RHD_DISPLAY_DCE, 12, 1, 0},
	{0x66a4, 12, 1, RADEON_VEGA20, CHIP_STD, "Radeon Vega 20", RHD_DISPLAY_DCE, 12, 1, 0},
	{0x66a7, 12, 1, RADEON_VEGA20, CHIP_STD, "Radeon Pro Vega 20", RHD_DISPLAY_DCE, 12, 1, 0},
	{0x66af, 12, 1, RADEON_VEGA20, CHIP_STD, "Radeon Pro Vega 20", RHD_DISPLAY_DCE, 12, 1, 0},
	// Raven and Picasso: DCN 1.0. Linux lists both as CHIP_RAVEN.
	{0x15dd, 0, 0, RADEON_RAVEN, CHIP_APU, "Radeon Raven", RHD_DISPLAY_DCN, 1, 0, 0},
	{0x15d8, 0, 0, RADEON_RAVEN, CHIP_APU, "Radeon Picasso", RHD_DISPLAY_DCN, 1, 0, 0},
	// Linux: 1636 and 164c load renoir firmware. 15e7 and 1638 load
	// green_sardine. All four are DCN 2.1. 0x15e7 is the Ryzen 5 5625U.
	{0x15e7, 0, 0, RADEON_RENOIR, CHIP_APU, "Radeon Renoir (Barcelo)", RHD_DISPLAY_DCN, 2, 1, 0},
	{0x1636, 0, 0, RADEON_RENOIR, CHIP_APU, "Radeon Renoir", RHD_DISPLAY_DCN, 2, 1, 0},
	{0x1638, 0, 0, RADEON_RENOIR, CHIP_APU, "Radeon Cezanne", RHD_DISPLAY_DCN, 2, 1, 0},
	{0x164c, 0, 0, RADEON_RENOIR, CHIP_APU, "Radeon Lucienne", RHD_DISPLAY_DCN, 2, 1, 0},
	// Navi 10: DCN 2.0.
	{0x7310, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 10", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x7312, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 10", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x7318, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 10", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x7319, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 10", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x731a, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 10", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x731b, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 10", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x731e, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 10", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x731f, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon RX 5600/5700", RHD_DISPLAY_DCN, 2, 0, 0},
	// Navi 14: DCN 2.0. 0x7340 was Haiku #17473.
	{0x7340, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon RX 5500", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x7341, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 14", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x7347, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 14", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x734f, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 14", RHD_DISPLAY_DCN, 2, 0, 0},
	// Sienna Cichlid, Navi 21: DCN 3.0. 0x73cf is not in Linux 5.15 or 6.12.
	{0x73a0, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73a1, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73a2, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73a3, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73ab, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73ae, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73bf, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon RX 6800/6900 XT", RHD_DISPLAY_DCN, 3, 0, 0},
	// Navy Flounder, Navi 22. resource_parse_asic_id has no separate case.
	{0x73c0, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navy Flounder", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73c1, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navy Flounder", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73c3, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navy Flounder", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73df, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon RX 6700 XT", RHD_DISPLAY_DCN, 3, 0, 0},
	// Dimgrey Cavefish, Navi 23: DCN 3.02.
	{0x73e0, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73e1, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73e2, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73ff, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	// Remaining explicit IDs from Linux amdgpu_drv.c v6.12. The name is the
	// chip comment in that file. One ID covers many retail cards.
	// Tahiti, Oland, Verde: same DCE generation as the rows above.
	{0x6791, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti", RHD_DISPLAY_DCE, 6, 0, 0},
	{0x6792, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti", RHD_DISPLAY_DCE, 6, 0, 0},
	{0x679b, 6, 0, RADEON_TAHITI, CHIP_STD, "Radeon HD Tahiti", RHD_DISPLAY_DCE, 6, 0, 0},
	{0x6617, 6, 4, RADEON_OLAND, CHIP_STD, "Radeon HD Oland", RHD_DISPLAY_DCE, 6, 4, 0},
	{0x6822, 6, 0, RADEON_CAPEVERDE, CHIP_STD, "Radeon HD Verde", RHD_DISPLAY_DCE, 6, 0, 0},
	// Hainan: no display. Linux marks these mobile.
	{0x6660, 0, 0, RADEON_HAINAN, CHIP_MOBILE, "Radeon Hainan", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x6663, 0, 0, RADEON_HAINAN, CHIP_MOBILE, "Radeon Hainan", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x6664, 0, 0, RADEON_HAINAN, CHIP_MOBILE, "Radeon Hainan", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x6665, 0, 0, RADEON_HAINAN, CHIP_MOBILE, "Radeon Hainan", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x6667, 0, 0, RADEON_HAINAN, CHIP_MOBILE, "Radeon Hainan", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x666f, 0, 0, RADEON_HAINAN, CHIP_MOBILE, "Radeon Hainan", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x6fdf, 11, 2, RADEON_POLARIS10, CHIP_STD, "Radeon Polaris 10", RHD_DISPLAY_DCE, 11, 2, 0},
	// Arcturus and Aldebaran: no display. chipsetID is Navi so the DCE
	// paths stay off. The display class is none.
	{0x738c, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Arcturus", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x7388, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Arcturus", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x738e, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Arcturus", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x7390, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Arcturus", RHD_DISPLAY_NONE, 0, 0, 0},
	// Navi 12: DCN 2.0.
	{0x7360, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 12", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x7362, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navi 12", RHD_DISPLAY_DCN, 2, 0, 0},
	{0x73a5, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73a8, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73a9, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73ac, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73ad, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73af, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Sienna Cichlid", RHD_DISPLAY_DCN, 3, 0, 0},
	// Yellow Carp: DCN 3.1 APU.
	{0x164d, 0, 0, RADEON_NAVI, CHIP_APU, "Radeon Yellow Carp", RHD_DISPLAY_DCN, 3, 1, 0},
	{0x1681, 0, 0, RADEON_NAVI, CHIP_APU, "Radeon Yellow Carp", RHD_DISPLAY_DCN, 3, 1, 0},
	{0x73da, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navy Flounder", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73db, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navy Flounder", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73dc, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navy Flounder", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73dd, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navy Flounder", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73de, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Navy Flounder", RHD_DISPLAY_DCN, 3, 0, 0},
	{0x73e3, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73e8, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73e9, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73ea, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73eb, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73ec, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73ed, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x73ef, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Dimgrey Cavefish", RHD_DISPLAY_DCN, 3, 0, 2},
	{0x7408, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Aldebaran", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x740c, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Aldebaran", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x740f, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Aldebaran", RHD_DISPLAY_NONE, 0, 0, 0},
	{0x7410, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Aldebaran", RHD_DISPLAY_NONE, 0, 0, 0},
	// Cyan Skillfish: DCN 2.01.
	{0x13fe, 0, 0, RADEON_NAVI, CHIP_APU, "Radeon Cyan Skillfish", RHD_DISPLAY_DCN, 2, 0, 1},
	{0x143f, 0, 0, RADEON_NAVI, CHIP_APU, "Radeon Cyan Skillfish", RHD_DISPLAY_DCN, 2, 0, 1},
	// Beige Goby: DCN 3.03. Last family with its own IDs in Linux v6.12.
	{0x7420, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Beige Goby", RHD_DISPLAY_DCN, 3, 0, 3},
	{0x7421, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Beige Goby", RHD_DISPLAY_DCN, 3, 0, 3},
	{0x7422, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Beige Goby", RHD_DISPLAY_DCN, 3, 0, 3},
	{0x7423, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Beige Goby", RHD_DISPLAY_DCN, 3, 0, 3},
	{0x7424, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Beige Goby", RHD_DISPLAY_DCN, 3, 0, 3},
	{0x743f, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon Beige Goby", RHD_DISPLAY_DCN, 3, 0, 3},
	// pciID 0 is not a device. A later AMD display or accelerator uses this
	// row. Linux calls that CHIP_IP_DISCOVERY and reads the version from
	// the GPU. This driver does not read that table yet, so the version
	// stays 0 and the DCE registers stay off.
	{0x0000, 0, 0, RADEON_NAVI, CHIP_STD, "Radeon IP discovery", RHD_DISPLAY_DCN, 0, 0, 0},
// SPDX-SnippetEnd
};


int32 api_version = B_CUR_DRIVER_API_VERSION;


char* gDeviceNames[MAX_CARDS + 1];
radeon_info* gDeviceInfo[MAX_CARDS];
pci_module_info* gPCI;
mutex gLock;


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static bool
radeon_hd_pci_class(const pci_info& info)
{
	if (info.vendor_id != VENDOR_ID_ATI)
		return false;

	// Linux CHIP_IP_DISCOVERY matches these three classes. 0x80 is
	// PCI_CLASS_DISPLAY_OTHER. 0x12 is PCI_CLASS_ACCELERATOR_PROCESSING.
	if (info.class_base == PCI_display
		&& (info.class_sub == PCI_vga || info.class_sub == 0x80))
		return true;
	if (info.class_base == 0x12 && info.class_sub == 0x00)
		return true;
	return false;
}
// SPDX-SnippetEnd


static status_t
get_next_radeon_hd(int32* _cookie, pci_info &info, uint32 &type)
{
	int32 index = *_cookie;

	// find devices

	for (; gPCI->get_nth_pci_info(index, &info) == B_OK; index++) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		// VGA, display-other, or a processing accelerator. The zero row
		// is the IP-discovery fallback, not a real PCI ID.
		if (!radeon_hd_pci_class(info))
			continue;

		uint32 discovery = ~(uint32)0;
		for (uint32 i = 0; i < sizeof(kSupportedDevices)
				/ sizeof(kSupportedDevices[0]); i++) {
			if (kSupportedDevices[i].pciID == 0) {
				discovery = i;
				continue;
			}
			if (info.device_id == kSupportedDevices[i].pciID) {
				type = i;
				*_cookie = index + 1;
				return B_OK;
			}
		}

		if (discovery != ~(uint32)0) {
			type = discovery;
			*_cookie = index + 1;
			return B_OK;
		}
// SPDX-SnippetEnd
	}

	return B_ENTRY_NOT_FOUND;
}


extern "C" const char**
publish_devices(void)
{
	TRACE("%s\n", __func__);
	return (const char**)gDeviceNames;
}


extern "C" status_t
init_hardware(void)
{
	TRACE("%s\n", __func__);

	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI);
	if (status != B_OK) {
		ERROR("%s: ERROR: pci module unavailable\n", __func__);
		return status;
	}

	int32 cookie = 0;
	uint32 type;
	pci_info info;
	status = get_next_radeon_hd(&cookie, info, type);

	put_module(B_PCI_MODULE_NAME);
	return status;
}


extern "C" status_t
init_driver(void)
{
	TRACE("%s\n", __func__);

	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI);
	if (status != B_OK) {
		ERROR("%s: ERROR: pci module unavailable\n", __func__);
		return status;
	}

	mutex_init(&gLock, "radeon hd ksync");

	// find devices

	int32 found = 0;

	for (int32 cookie = 0; found < MAX_CARDS;) {
		pci_info* info = (pci_info*)malloc(sizeof(pci_info));
		if (info == NULL)
			break;

		uint32 type;
		status = get_next_radeon_hd(&cookie, *info, type);
		if (status < B_OK) {
			free(info);
			break;
		}

		// create device names & allocate device info structure

		char name[64];
		sprintf(name, "graphics/radeon_hd_%02x%02x%02x",
			info->bus, info->device,
			info->function);

		gDeviceNames[found] = strdup(name);
		if (gDeviceNames[found] == NULL)
			break;

		gDeviceInfo[found] = (radeon_info*)malloc(sizeof(radeon_info));
		if (gDeviceInfo[found] == NULL) {
			free(gDeviceNames[found]);
			break;
		}

		// initialize the structure for later use

		memset(gDeviceInfo[found], 0, sizeof(radeon_info));
		gDeviceInfo[found]->init_status = B_NO_INIT;
		gDeviceInfo[found]->id = found;
		gDeviceInfo[found]->pci = info;
		gDeviceInfo[found]->registers = info->u.h0.base_registers[0];
		gDeviceInfo[found]->pciID = kSupportedDevices[type].pciID;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		if (gDeviceInfo[found]->pciID == 0)
			gDeviceInfo[found]->pciID = info->device_id;
		gDeviceInfo[found]->doorbell_area = -1;
// SPDX-SnippetEnd
		gDeviceInfo[found]->deviceName = kSupportedDevices[type].deviceName;
		gDeviceInfo[found]->chipsetID = kSupportedDevices[type].chipsetID;
		gDeviceInfo[found]->dceMajor = kSupportedDevices[type].dceMajor;
		gDeviceInfo[found]->dceMinor = kSupportedDevices[type].dceMinor;
		gDeviceInfo[found]->displayClass = kSupportedDevices[type].displayClass;
		gDeviceInfo[found]->displayMajor = kSupportedDevices[type].displayMajor;
		gDeviceInfo[found]->displayMinor = kSupportedDevices[type].displayMinor;
		gDeviceInfo[found]->displayStepping = kSupportedDevices[type].displayStepping;
		gDeviceInfo[found]->chipsetFlags = kSupportedDevices[type].chipsetFlags;

		ERROR("%s: GPU(%" B_PRId32 ") %s, revision = 0x%x\n", __func__, found,
			kSupportedDevices[type].deviceName, info->revision);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		if (kSupportedDevices[type].pciID == 0) {
			ERROR("%s: pci 1002:%04x claimed by IP discovery, "
				"display version unknown\n", __func__, info->device_id);
		}
// SPDX-SnippetEnd

		found++;
	}

	gDeviceNames[found] = NULL;

	if (found == 0) {
		mutex_destroy(&gLock);
		put_module(B_AGP_GART_MODULE_NAME);
		put_module(B_PCI_MODULE_NAME);
		ERROR("%s: no supported devices found\n", __func__);
		return ENODEV;
	}

	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	TRACE("%s\n", __func__);

	mutex_destroy(&gLock);

	// free device related structures
	char* name;
	for (int32 index = 0; (name = gDeviceNames[index]) != NULL; index++) {
		free(gDeviceInfo[index]);
		free(name);
	}

	put_module(B_PCI_MODULE_NAME);
}


extern "C" device_hooks*
find_device(const char* name)
{
	int index;

	TRACE("%s\n", __func__);

	for (index = 0; gDeviceNames[index] != NULL; index++) {
		if (!strcmp(name, gDeviceNames[index]))
			return &gDeviceHooks;
	}

	ERROR("%s: %s wasn't found!\n", __func__, name);
	return NULL;
}

