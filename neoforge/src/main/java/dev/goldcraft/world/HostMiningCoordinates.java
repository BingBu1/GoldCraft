package dev.goldcraft.world;

import dev.goldcraft.bridge.MapMining;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.util.math.BlockPos;
import java.util.Objects;

/** Mining identity stays in the target frame; vanilla hooks receive world positions. */
final class HostMiningCoordinates {
    private HostMiningCoordinates() {}

    // The ray cell is provisional. Only Sample identifies the native-authorized cell.
    record Target(int slot, int serial, int model, BlockPos worldCell, MapMining.Cell rayCell, long edits) {}
    record Sample(int slot, int serial, int model, MapMining.Cell cell, long edits) {}

    static Target target(int slot, int serial, int model, BlockPos worldCell,
                         MapMining.Cell rayCell, long edits) {
        if (slot != 0 && rayCell == null) return null;
        return new Target(slot, serial, model, slot == 0 ? worldCell : null,
                          slot == 0 ? null : rayCell, edits);
    }

    static Sample sample(Target target, MapMining.Surface surface) {
        return new Sample(target.slot(), target.serial(), target.model(),
                          surface.editRevision() == 0 ? null : surface.cell(), surface.editRevision());
    }

    static boolean sameFrame(Target first, Target second) {
        return first != null && second != null && first.slot() == second.slot()
            && first.serial() == second.serial() && first.model() == second.model()
            && first.edits() == second.edits() && Objects.equals(first.worldCell(), second.worldCell());
    }

    static BlockPos worldCell(Target target, Sample sample, BlockPos currentHit) {
        // Existing static-world samples use a canonical GS world cell. Dynamic
        // cells cannot be converted without the brush transform: use the current
        // world ray hit instead, including while a material sample is cached.
        if (target.slot() == 0 && sample != null && sample.cell() != null) {
            return worldCell(sample.cell());
        }
        return currentHit;
    }

    static MapMining.Cell rayCell(BspMap.Vec from, BspMap.Vec to, BspMap.Trace trace) {
        return cell(from, to, trace.normal(), fraction(from, to, trace), trace.planeDistance());
    }

    static BlockPos worldCell(BspMap.Vec from, BspMap.Vec to, BspMap.Trace trace, HostWorldState.Brush brush) {
        double nx = trace.normal().x(), ny = trace.normal().y(), nz = trace.normal().z();
        double ox = 0, oy = 0, oz = 0;
        if (brush != null) {
            double pitch = Math.toRadians(brush.angles().x()), yaw = Math.toRadians(brush.angles().y());
            double roll = Math.toRadians(brush.angles().z());
            double sp = Math.sin(pitch), cp = Math.cos(pitch), sy = Math.sin(yaw), cy = Math.cos(yaw);
            double sr = Math.sin(roll), cr = Math.cos(roll);
            double x = cp*cy*nx + (sr*sp*cy-cr*sy)*ny + (cr*sp*cy+sr*sy)*nz;
            double y = cp*sy*nx + (sr*sp*sy+cr*cy)*ny + (cr*sp*sy-sr*cy)*nz;
            double z = -sp*nx + sr*cp*ny + cr*cp*nz;
            nx = x; ny = y; nz = z;
            ox = brush.origin().x(); oy = brush.origin().y(); oz = brush.origin().z();
        }
        // Use the original plane and world ray in double precision relative to
        // origin. Reusing float local endpoints can cross a world grid boundary.
        // Placement's empty-side BlockPos must never reach permission/tool hooks.
        double fraction = fraction(from, to, trace, nx, ny, nz, ox, oy, oz);
        int x = axis(from, to, nx, fraction, trace.planeDistance(), 0);
        int y = axis(from, to, ny, fraction, trace.planeDistance(), 1);
        int z = axis(from, to, nz, fraction, trace.planeDistance(), 2);
        return new BlockPos(x, z + (int)Wire.Y_OFFSET, -y - 1);
    }

    private static BlockPos worldCell(MapMining.Cell cell) {
        return new BlockPos(cell.x(), cell.z() + (int)Wire.Y_OFFSET, -cell.y() - 1);
    }

    private static double fraction(BspMap.Vec from, BspMap.Vec to, BspMap.Trace trace) {
        return fraction(from, to, trace, trace.normal().x(), trace.normal().y(), trace.normal().z(), 0, 0, 0);
    }

    private static double fraction(BspMap.Vec from, BspMap.Vec to, BspMap.Trace trace,
                                   double nx, double ny, double nz, double ox, double oy, double oz) {
        if (trace.startSolid() || trace.allSolid() || trace.fraction() >= 1)
            throw new IllegalArgumentException("Mining ray has no surface");
        // Match mining_cell.hpp: intersect the original local plane, not the
        // endpoint backed off by DIST_EPSILON. This is never an authorization.
        double distance = trace.planeDistance(), speed = 0, length = 0;
        for (int i = 0; i < 3; ++i) {
            double origin = i == 0 ? ox : i == 1 ? oy : oz;
            double a = (double)from.axis(i) - origin, b = (double)to.axis(i) - origin;
            double n = i == 0 ? nx : i == 1 ? ny : nz;
            if (!Double.isFinite(a) || !Double.isFinite(b) || !Double.isFinite(n))
                throw new IllegalArgumentException("Nonfinite mining surface");
            distance -= a * n;
            speed += (b - a) * n;
            length += n * n;
        }
        if (!Double.isFinite(distance) || length < .98 || length > 1.02 || speed >= -1e-6)
            throw new IllegalArgumentException("Invalid mining ray plane");
        double fraction = distance / speed;
        if (fraction < 0 || fraction > 1.000001)
            throw new IllegalArgumentException("Mining plane outside ray");
        return fraction;
    }

    private static MapMining.Cell cell(BspMap.Vec from, BspMap.Vec to, BspMap.Vec normal,
                                      double fraction, double planeDistance) {
        return new MapMining.Cell(axis(from, to, normal.x(), fraction, planeDistance, 0),
            axis(from, to, normal.y(), fraction, planeDistance, 1), axis(from, to, normal.z(), fraction, planeDistance, 2));
    }

    private static int axis(BspMap.Vec from, BspMap.Vec to, double normal, double fraction,
                            double planeDistance, int axis) {
        double a = from.axis(axis), b = to.axis(axis);
        double point = a + (b - a) * fraction;
        if (!Double.isFinite(point) || point < -32768 || point >= 32768)
            throw new IllegalArgumentException("Mining ray outside cell grid");
        double grid = point / Wire.UNITS_PER_BLOCK;
        double boundary = Math.rint(grid);
        double rounding = 32 * Math.ulp(1.0) *
            (1 + Math.abs(a) + Math.abs(b) + Math.abs(planeDistance)) / Wire.UNITS_PER_BLOCK;
        if (Math.abs(grid - boundary) <= rounding) grid = boundary;
        double index = Math.floor(grid);
        if (normal > 0 && index == grid) --index;
        if (index < -1024 || index >= 1024)
            throw new IllegalArgumentException("Mining solid side outside cell grid");
        return (int)index;
    }
}
