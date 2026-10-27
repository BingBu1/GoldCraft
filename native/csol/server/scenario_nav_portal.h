// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class CNavArea;
class Vector;

// CSO mode 15's portal selection. Direction is the SDK NavDirType value
// (NORTH/EAST/SOUTH/WEST). Writes XY only, preserving the supplied Z, as the
// path-position builder applies the 5-unit step and source-area height later.
// Uses one CRT rand()%10 sample after validating finite area geometry.
bool CSOL_ComputeScenarioPortal(const CNavArea& from, const CNavArea& to,
    int direction, Vector& output);
