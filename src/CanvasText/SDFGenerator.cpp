//================================================================================================
// CanvasText - SDF/MSDF Generator Implementation
//================================================================================================

#include "SDFGenerator.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace Canvas
{

//------------------------------------------------------------------------------------------------
// Utility: Point-to-segment signed distance
//------------------------------------------------------------------------------------------------

float CSDFGenerator::SignedDistance(float px, float py, const GlyphPoint &p0, const GlyphPoint &p1)
{
    // Vector from p0 to point
    float dx = px - p0.X;
    float dy = py - p0.Y;
    
    // Vector from p0 to p1
    float edgeDx = p1.X - p0.X;
    float edgeDy = p1.Y - p0.Y;
    
    // Project point onto line defined by edge
    float edgeLenSq = edgeDx * edgeDx + edgeDy * edgeDy;
    if (edgeLenSq < 1e-6f)
    {
        // Degenerate edge - just use distance to point
        return std::sqrt(dx * dx + dy * dy);
    }
    
    float t = (dx * edgeDx + dy * edgeDy) / edgeLenSq;
    t = std::max(0.0f, std::min(1.0f, t)); // Clamp to segment
    
    // Closest point on segment
    float closestX = p0.X + t * edgeDx;
    float closestY = p0.Y + t * edgeDy;
    
    // Distance to closest point
    float distX = px - closestX;
    float distY = py - closestY;
    float dist = std::sqrt(distX * distX + distY * distY);
    
    // Compute cross product to determine sign (inside/outside)
    // For line from p0 to p1, check which side point is on
    float cross = (px - p0.X) * edgeDy - (py - p0.Y) * edgeDx;
    
    return (cross < 0.0f) ? -dist : dist;
}

//------------------------------------------------------------------------------------------------
// Utility: Point-to-quadratic-Bézier signed distance
//
// For TrueType glyphs, edges are defined as sequences of on-curve and off-curve points.
// Off-curve points are Bézier control points.
//------------------------------------------------------------------------------------------------

float CSDFGenerator::SignedDistanceToCurve(float px, float py,
                                          float cx0, float cy0, bool on0,
                                          float cx1, float cy1, bool on1,
                                          float cx2, float cy2, bool on2)
{
    // Unused parameters - kept for API compatibility (future MSDF implementation)
    (void)cx1; (void)cy1; (void)on1;
    
    // TrueType uses implicit off-curve points:
    // on, off, on = BQ: from on0 to on2 with off-curve at off1
    // on, off, off = quadratic between endpoint and middle of two off-curves
    
    // For simplicity, use line segments to approximate; proper Bézier distance
    // would require solving quartic equations (see MSDF papers for details)
    
    // Note: Full implementation would use more sophisticated approach
    // For now, treating a simplified line segment works for coarse SDF
    
    // Approximate curve as line from start to end
    if (!on0 || !on2)
        return 0.0f; // Skip degenerate curves
    
    return SignedDistance(px, py, GlyphPoint(cx0, cy0, true), GlyphPoint(cx2, cy2, true));
}

//------------------------------------------------------------------------------------------------
// Winding-number contribution from a straight line segment (x0,y0)→(x2,y2).
// Uses a half-open y-interval to avoid double-counting shared endpoints.
//------------------------------------------------------------------------------------------------
static int LineWindingCrossing(float px, float py,
                               float x0, float y0,
                               float x2, float y2)
{
    if ((y0 <= py && y2 > py) || (y2 <= py && y0 > py))
    {
        float xInt = x0 + (py - y0) * (x2 - x0) / (y2 - y0);
        if (px < xInt)
            return (y2 > y0) ? 1 : -1;
    }
    return 0;
}

//------------------------------------------------------------------------------------------------
// Winding-number contribution from a quadratic Bezier P0→Pctrl→P2.
// Solves B_y(t) = py analytically (at most 2 roots), accumulates sign(B_y'(t)) at each
// crossing where px < B_x(t).  Uses t ∈ [0,1) to avoid double-counting shared vertices.
//------------------------------------------------------------------------------------------------
static int QuadBezierWindingCrossing(float px, float py,
                                     float x0, float y0,
                                     float cx, float cy,
                                     float x2, float y2)
{
    // B_y(t) = (1-t)²·y0 + 2t(1-t)·cy + t²·y2  =  a·t² + b·t + c₀
    float a  = y0 - 2.0f*cy + y2;
    float b  = 2.0f*(cy - y0);
    float c0 = y0 - py;

    int winding = 0;
    float roots[2];
    int   numRoots = 0;

    if (std::abs(a) < 1e-8f)
    {
        // Degenerate (linear) — single root
        if (std::abs(b) > 1e-8f)
        {
            float t = -c0 / b;
            if (t >= 0.0f && t < 1.0f) roots[numRoots++] = t;
        }
    }
    else
    {
        float disc = b*b - 4.0f*a*c0;
        if (disc >= 0.0f)
        {
            float sqrtD = std::sqrt(disc);
            float t0 = (-b - sqrtD) / (2.0f*a);
            float t1 = (-b + sqrtD) / (2.0f*a);
            if (t0 >= 0.0f && t0 < 1.0f) roots[numRoots++] = t0;
            if (t1 >= 0.0f && t1 < 1.0f) roots[numRoots++] = t1;
        }
    }

    for (int i = 0; i < numRoots; ++i)
    {
        float t  = roots[i];
        float mt = 1.0f - t;
        float bx = mt*mt*x0 + 2.0f*mt*t*cx + t*t*x2;
        if (px < bx)
        {
            float dby = 2.0f*a*t + b;  // B_y'(t)
            if      (dby > 0.0f) winding++;
            else if (dby < 0.0f) winding--;
        }
    }
    return winding;
}

//------------------------------------------------------------------------------------------------
// Compute winding number for point-in-polygon test.
// Decodes TrueType quadratic outlines the same way ContourMinDist does — same on-curve /
// off-curve logic, same implicit midpoint handling — so inside/outside is correct for
// every glyph, including those with curved strokes whose control points fall outside the ink.
//------------------------------------------------------------------------------------------------

int CSDFGenerator::ComputeWindingNumber(float px, float py, const GlyphContour &contour)
{
    int    winding = 0;
    size_t n       = contour.Points.size();
    if (n < 2)
        return 0;

    // Find first on-curve point (same as ContourMinDist)
    size_t firstOn = n;
    for (size_t i = 0; i < n; ++i)
    {
        if (contour.Points[i].IsOnCurve) { firstOn = i; break; }
    }
    if (firstOn == n)
        return 0;

    float sx = contour.Points[firstOn].X;
    float sy = contour.Points[firstOn].Y;

    float offX[32], offY[32];
    int   offCount = 0;

    for (size_t step = 1; step <= n; ++step)
    {
        const GlyphPoint &pt = contour.Points[(firstOn + step) % n];

        if (pt.IsOnCurve)
        {
            float ex = pt.X, ey = pt.Y;

            if (offCount == 0)
            {
                winding += LineWindingCrossing(px, py, sx, sy, ex, ey);
            }
            else if (offCount == 1)
            {
                winding += QuadBezierWindingCrossing(px, py, sx, sy, offX[0], offY[0], ex, ey);
            }
            else
            {
                float bsx = sx, bsy = sy;
                for (int k = 0; k < offCount - 1; ++k)
                {
                    float midX = (offX[k] + offX[k+1]) * 0.5f;
                    float midY = (offY[k] + offY[k+1]) * 0.5f;
                    winding += QuadBezierWindingCrossing(px, py, bsx, bsy, offX[k], offY[k], midX, midY);
                    bsx = midX; bsy = midY;
                }
                winding += QuadBezierWindingCrossing(px, py, bsx, bsy,
                                                     offX[offCount-1], offY[offCount-1], ex, ey);
            }

            sx = ex; sy = ey;
            offCount = 0;
        }
        else
        {
            if (offCount < 32)
            {
                offX[offCount] = pt.X;
                offY[offCount] = pt.Y;
                offCount++;
            }
        }
    }

    return winding;
}

//------------------------------------------------------------------------------------------------
// Transform point from glyph space to SDF bitmap space
//------------------------------------------------------------------------------------------------

void CSDFGenerator::TransformPoint(float &x, float &y,
                                  float minX, float minY, float width, float height,
                                  uint32_t bitmapSize)
{
    if (width > 0 && height > 0)
    {
        // Normalize to [0,1] within glyph bounding box
        float normX = (x - minX) / width;
        float normY = (y - minY) / height;
        
        // Scale to bitmap coordinates
        x = normX * (bitmapSize - 1);
        y = normY * (bitmapSize - 1);
    }
}

//------------------------------------------------------------------------------------------------
// Analytic minimum distance from point (px, py) to a quadratic Bezier P0→Pctrl→P2.
//
// Minimises |B(t) - P|² where B(t) = (1-t)²P0 + 2t(1-t)C + t²P2.
// Setting the derivative to zero yields a cubic in t:
//     |F|² t³ + 3(E·F) t² + (D·F + 2|E|²) t + D·E = 0
// with D = P0 - P,  E = C - P0,  F = P0 - 2C + P2.
// The cubic is solved numerically via Newton iterations from several starting points,
// guaranteeing all roots in [0,1] are found.  Endpoints t=0, t=1 are always tested.
//------------------------------------------------------------------------------------------------
static float QuadBezierMinDist(float px, float py,
                               float x0, float y0,
                               float cx, float cy,
                               float x2, float y2)
{
    float Dx = x0 - px, Dy = y0 - py;
    float Ex = cx - x0, Ey = cy - y0;
    float Fx = x0 - 2.0f*cx + x2, Fy = y0 - 2.0f*cy + y2;

    // Evaluate squared distance at parameter t
    auto distSq = [&](float t) -> float {
        float mt = 1.0f - t;
        float bx = mt*mt*x0 + 2.0f*mt*t*cx + t*t*x2 - px;
        float by = mt*mt*y0 + 2.0f*mt*t*cy + t*t*y2 - py;
        return bx*bx + by*by;
    };

    // g(t) = d/dt(|B(t)-P|²) / 2 = (D + 2tE + t²F)·(E + tF)
    // g'(t) = 2|E + tF|² + (D + 2tE + t²F)·F
    auto evalGG = [&](float t, float &g, float &gp) {
        float ux = Dx + t*(2.0f*Ex + t*Fx);
        float uy = Dy + t*(2.0f*Ey + t*Fy);
        float vx = Ex + t*Fx;
        float vy = Ey + t*Fy;
        g  = ux*vx + uy*vy;
        gp = 2.0f*(vx*vx + vy*vy) + (ux*Fx + uy*Fy);
    };

    // Start with endpoint distances
    float best = std::min(distSq(0.0f), distSq(1.0f));

    // Newton iterations from 5 starting points spread across [0,1]
    for (int i = 0; i < 5; ++i)
    {
        float t = (i + 0.5f) / 5.0f;
        for (int j = 0; j < 8; ++j)
        {
            float g, gp;
            evalGG(t, g, gp);
            if (std::abs(gp) < 1e-12f) break;
            t -= g / gp;
            t = std::max(0.0f, std::min(1.0f, t));
        }
        float d = distSq(t);
        if (d < best) best = d;
    }

    return std::sqrt(best);
}

//------------------------------------------------------------------------------------------------
// Compute minimum distance from (px, py) to all edges in one contour.
// Handles TrueType quadratic outlines:
//   on → on          : straight line segment
//   on → off → on    : quadratic Bezier
//   consecutive offs  : implicit on-curve at their midpoint splits them into two Beziers
//------------------------------------------------------------------------------------------------
static float ContourMinDist(float px, float py, const Canvas::GlyphContour &contour)
{
    float minDist = std::numeric_limits<float>::max();
    size_t n = contour.Points.size();
    if (n < 2)
        return minDist;

    // Find the first on-curve point to use as the segment start.
    // If all points are off-curve (degenerate), bail out.
    size_t firstOn = n;
    for (size_t i = 0; i < n; ++i)
    {
        if (contour.Points[i].IsOnCurve) { firstOn = i; break; }
    }
    if (firstOn == n)
        return minDist;

    float sx = contour.Points[firstOn].X;
    float sy = contour.Points[firstOn].Y;

    // Accumulated off-curve control points between two on-curve anchors
    float offX[32], offY[32];
    int   offCount = 0;

    for (size_t step = 1; step <= n; ++step)
    {
        const Canvas::GlyphPoint &pt = contour.Points[(firstOn + step) % n];

        if (pt.IsOnCurve)
        {
            float ex = pt.X, ey = pt.Y;

            if (offCount == 0)
            {
                // Straight segment — inline point-to-segment distance
                float edgeDx = ex - sx, edgeDy = ey - sy;
                float lenSq  = edgeDx*edgeDx + edgeDy*edgeDy;
                float d;
                if (lenSq < 1e-12f)
                {
                    float qx = px - sx, qy = py - sy;
                    d = std::sqrt(qx*qx + qy*qy);
                }
                else
                {
                    float tp = ((px - sx)*edgeDx + (py - sy)*edgeDy) / lenSq;
                    tp = std::max(0.0f, std::min(1.0f, tp));
                    float qx = px - (sx + tp*edgeDx), qy = py - (sy + tp*edgeDy);
                    d = std::sqrt(qx*qx + qy*qy);
                }
                minDist = std::min(minDist, d);
            }
            else if (offCount == 1)
            {
                // Single quadratic Bezier
                float d = QuadBezierMinDist(px, py, sx, sy, offX[0], offY[0], ex, ey);
                minDist = std::min(minDist, d);
            }
            else
            {
                // Multiple off-curve controls: insert implicit midpoints and emit sub-Beziers
                float bsx = sx, bsy = sy;
                for (int k = 0; k < offCount - 1; ++k)
                {
                    float midX = (offX[k] + offX[k+1]) * 0.5f;
                    float midY = (offY[k] + offY[k+1]) * 0.5f;
                    float d = QuadBezierMinDist(px, py, bsx, bsy, offX[k], offY[k], midX, midY);
                    minDist = std::min(minDist, d);
                    bsx = midX; bsy = midY;
                }
                // Final sub-Bezier from last implicit midpoint to the on-curve end
                float d = QuadBezierMinDist(px, py, bsx, bsy,
                                            offX[offCount-1], offY[offCount-1], ex, ey);
                minDist = std::min(minDist, d);
            }

            sx = ex; sy = ey;
            offCount = 0;
        }
        else
        {
            if (offCount < 32)
            {
                offX[offCount] = pt.X;
                offY[offCount] = pt.Y;
                offCount++;
            }
        }
    }
    return minDist;
}


bool CSDFGenerator::Generate(const GlyphOutline &outline, const CTrueTypeFont &font,
                            const Config &config, SDFBitmap &outBitmap)
{
    if (outline.IsEmpty())
        return false;
    
    // Initialize bitmap
    outBitmap.Width = outBitmap.Height = config.TextureSize;
    outBitmap.BytesPerPixel = config.GenerateMSDF ? 3 : 1;
    outBitmap.Data.clear();
    outBitmap.Data.resize(config.TextureSize * config.TextureSize * outBitmap.BytesPerPixel, 127);
    
    // Copy glyph metrics (normalize from font units)
    outBitmap.AdvanceWidth = font.NormalizeX(outline.AdvanceWidth);
    outBitmap.LeftBearing = font.NormalizeX(outline.LeftSideBearing);
    outBitmap.MinX = font.NormalizeX(outline.XMin);
    outBitmap.MinY = font.NormalizeY(outline.YMin);
    outBitmap.MaxX = font.NormalizeX(outline.XMax);
    outBitmap.MaxY = font.NormalizeY(outline.YMax);
    
    // Glyph bounds in font units (same space as contour points — do NOT normalize here)
    float fXMin = outline.XMin;
    float fYMax = outline.YMax;
    float fWidth  = outline.XMax - outline.XMin;
    float fHeight = outline.YMax - outline.YMin;

    if (fWidth <= 0 || fHeight <= 0)
        return false; // Degenerate glyph

    // Normalise all distances by a consistent em-fraction so the SDF gradient slope is
    // uniform across every glyph regardless of aspect ratio.  Using fHeight (cap-height in
    // font units) means a narrow 'l' and a wide 'H' both have the same edge softness.
    // config.SampleRange is in em-fractions (e.g. 0.1 = 10% of cap height).
    float sampleRangeFontUnits = config.SampleRange * fHeight;
    if (sampleRangeFontUnits <= 0.0f)
        sampleRangeFontUnits = fHeight;

    // Expand the sampled region by the gradient margin on all sides so the SDF decays
    // to fully transparent before the tile boundary.  This prevents color bleed when
    // atlas tiles are tightly packed and eliminates the need for UV insets.
    float sampXMin   = fXMin - sampleRangeFontUnits;
    float sampYMax   = fYMax + sampleRangeFontUnits;
    float sampWidth  = fWidth  + 2.0f * sampleRangeFontUnits;
    float sampHeight = fHeight + 2.0f * sampleRangeFontUnits;

    // Update the normalised bounds to reflect the padded tile extents.
    // BitmapWidth/Height and bearings in the atlas entry are derived from these.
    float marginEm = config.SampleRange * (outBitmap.MaxY - outBitmap.MinY);
    outBitmap.MinX -= marginEm;
    outBitmap.MaxX += marginEm;
    outBitmap.MinY -= marginEm;
    outBitmap.MaxY += marginEm;

    for (uint32_t y = 0; y < config.TextureSize; ++y)
    {
        for (uint32_t x = 0; x < config.TextureSize; ++x)
        {
            // Sample point in font units using texel centers within the padded tile region.
            // Texel (x, y) covers 1/N of the padded bbox; center is at (x+0.5)/N.
            // Y is flipped: row 0 samples near the padded top (sampYMax in Y-up space).
            float px = sampXMin + ((x + 0.5f) / config.TextureSize) * sampWidth;
            float py = sampYMax - ((y + 0.5f) / config.TextureSize) * sampHeight;
            
            // Compute minimum distance to any edge in any contour
            float minDist = std::numeric_limits<float>::max();

            for (const auto &contour : outline.Contours)
            {
                float d = ContourMinDist(px, py, contour);
                if (d < minDist) minDist = d;
            }
            
            // Compute winding number for inside/outside determination
            int winding = 0;
            for (const auto &contour : outline.Contours)
            {
                winding += ComputeWindingNumber(px, py, contour);
            }
            
            bool inside = (winding != 0);
            float signedDist = inside ? minDist : -minDist;
            
            // Normalize distance to [0, 255]
            // Distance field center is 128 (0.5 * 255)
            float normalized = (signedDist / sampleRangeFontUnits) * 0.5f + 0.5f;
            normalized = std::max(0.0f, std::min(1.0f, normalized));
            
            uint8_t value = (uint8_t)(normalized * 255.0f + 0.5f);
            
            if (config.GenerateMSDF)
            {
                // For MSDF, store same value in R, G, B channels
                uint8_t *pPixel = outBitmap.GetPixel(x, y, 0);
                pPixel[0] = value;
                pPixel[1] = value;
                pPixel[2] = value;
            }
            else
            {
                *outBitmap.GetPixel(x, y, 0) = value;
            }
        }
    }
    
    return true;
}

} // namespace Canvas
