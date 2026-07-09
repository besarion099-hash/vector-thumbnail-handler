// DXF-Miniaturen: eigener Parser fuer Text-DXF (Gruppencode-Paare) mit den
// gaengigen 2D-Entities, gesampelt zu Polylinien und mit Direct2D gezeichnet.
// Nicht darstellbare Entities (TEXT, 3D, HATCH-Fuellungen ...) werden
// uebersprungen - lieber eine unvollstaendige Miniatur als keine.

#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "render_common.h"
#include "renderers.h"

using Microsoft::WRL::ComPtr;

namespace {

const double kPi = 3.14159265358979323846;
const size_t kMaxPoints = 2'000'000;   // Sicherheitslimit gesampelte Punkte
const int kMaxInsertDepth = 8;

struct Pt {
    double x = 0, y = 0;
};

struct Poly {
    std::vector<Pt> pts;
    int aci = 256; // AutoCAD Color Index; 256 = ByLayer
    std::string layer;
};

struct CodePair {
    int code;
    std::string value;
};

struct RawEntity {
    std::string type;
    std::vector<CodePair> codes;
};

struct Xform {
    // p' = R*S*p + t  (Rotation R um Winkel rot, Skalierung sx/sy, Translation)
    double sx = 1, sy = 1, rot = 0, tx = 0, ty = 0;

    Pt Apply(const Pt& p) const
    {
        const double c = cos(rot), s = sin(rot);
        const double x = p.x * sx, y = p.y * sy;
        return { x * c - y * s + tx, x * s + y * c + ty };
    }
};

double ToD(const std::string& s)
{
    return strtod(s.c_str(), nullptr);
}

int ToI(const std::string& s)
{
    return static_cast<int>(strtol(s.c_str(), nullptr, 10));
}

std::string Trim(const char* begin, const char* end)
{
    while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == '\r'))
        ++begin;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        --end;
    return std::string(begin, end);
}

// AutoCAD Color Index -> RGB (Grundfarben; 7 wird auf Schwarz gelegt, weil
// auf weissem Hintergrund gezeichnet wird)
COLORREF AciToRgb(int aci)
{
    switch (aci) {
        case 1: return RGB(216, 0, 0);     // rot
        case 2: return RGB(180, 160, 0);   // gelb (abgedunkelt)
        case 3: return RGB(0, 160, 0);     // gruen
        case 4: return RGB(0, 160, 176);   // cyan
        case 5: return RGB(0, 0, 216);     // blau
        case 6: return RGB(200, 0, 200);   // magenta
        case 7: return RGB(0, 0, 0);       // weiss/schwarz -> schwarz
        case 8: return RGB(128, 128, 128);
        case 9: return RGB(170, 170, 170);
        default: break;
    }
    if (aci >= 250 && aci <= 255) {
        int g = 40 + (aci - 250) * 40;
        return RGB(g, g, g);
    }
    return RGB(0, 0, 0);
}

class DxfModel {
public:
    std::vector<Poly> polys;
    std::map<std::string, std::vector<RawEntity>> blocks;
    std::map<std::string, Pt> blockBase;
    std::map<std::string, int> layerColor;
    size_t pointBudget = kMaxPoints;

    void AddArcSamples(std::vector<Pt>& out, double cxr, double cyr, double rx,
                       double ry, double axisRot, double a0, double a1)
    {
        if (a1 < a0)
            a1 += 2 * kPi;
        const double span = a1 - a0;
        int steps = static_cast<int>(span / (kPi / 45.0)) + 2; // ~4 Grad
        steps = std::min(steps, 720);
        const double ca = cos(axisRot), sa = sin(axisRot);
        for (int i = 0; i <= steps; ++i) {
            const double t = a0 + span * i / steps;
            const double px = rx * cos(t), py = ry * sin(t);
            out.push_back({ cxr + px * ca - py * sa, cyr + px * sa + py * ca });
        }
    }

    // Bulge-Segment: Kreisbogen zwischen p1 und p2, eingeschlossener Winkel
    // = 4*atan(bulge)
    void AddBulgeSamples(std::vector<Pt>& out, const Pt& p1, const Pt& p2,
                         double bulge)
    {
        if (fabs(bulge) < 1e-9) {
            out.push_back(p2);
            return;
        }
        const double theta = 4.0 * atan(bulge);
        const double dx = p2.x - p1.x, dy = p2.y - p1.y;
        const double chord = sqrt(dx * dx + dy * dy);
        if (chord < 1e-12) {
            out.push_back(p2);
            return;
        }
        const double r = chord / (2.0 * fabs(sin(theta / 2.0)));
        // Mittelpunkt: senkrecht zur Sehne
        const double mx = (p1.x + p2.x) / 2.0, my = (p1.y + p2.y) / 2.0;
        double h2 = r * r - chord * chord / 4.0;
        const double h = h2 > 0 ? sqrt(h2) : 0;
        const double ux = -dy / chord, uy = dx / chord; // Einheitsnormale
        const double sign = (bulge > 0) ? 1.0 : -1.0;
        const double ccx = mx - sign * h * ux * ((fabs(theta) > kPi) ? -1.0 : 1.0);
        const double ccy = my - sign * h * uy * ((fabs(theta) > kPi) ? -1.0 : 1.0);

        double a0 = atan2(p1.y - ccy, p1.x - ccx);
        double a1 = a0 + theta;
        int steps = static_cast<int>(fabs(theta) / (kPi / 45.0)) + 2;
        steps = std::min(steps, 720);
        for (int i = 1; i <= steps; ++i) {
            const double t = a0 + (a1 - a0) * i / steps;
            out.push_back({ ccx + r * cos(t), ccy + r * sin(t) });
        }
    }

    // De-Boor-Auswertung einer B-Spline-Kurve
    static Pt DeBoor(int degree, const std::vector<Pt>& ctrl,
                     const std::vector<double>& knots, double t)
    {
        int n = static_cast<int>(ctrl.size()) - 1;
        int k = degree;
        // Knotenintervall suchen
        int span = degree;
        for (int i = degree; i <= n; ++i) {
            if (t >= knots[i] && t <= knots[i + 1] + 1e-12) {
                span = i;
                break;
            }
        }
        std::vector<Pt> d(k + 1);
        for (int j = 0; j <= k; ++j)
            d[j] = ctrl[span - k + j];
        for (int r = 1; r <= k; ++r) {
            for (int j = k; j >= r; --j) {
                const double den = knots[span - k + j + k - r + 1] - knots[span - k + j];
                const double alpha = den > 1e-12 ? (t - knots[span - k + j]) / den : 0;
                d[j].x = (1 - alpha) * d[j - 1].x + alpha * d[j].x;
                d[j].y = (1 - alpha) * d[j - 1].y + alpha * d[j].y;
            }
        }
        return d[k];
    }

    void EmitPoly(std::vector<Pt>&& pts, int aci, const std::string& layer,
                  const Xform& xf)
    {
        if (pts.size() < 2)
            return;
        if (pointBudget < pts.size())
            return;
        pointBudget -= pts.size();
        Poly p;
        p.pts.reserve(pts.size());
        for (const Pt& q : pts)
            p.pts.push_back(xf.Apply(q));
        p.aci = aci;
        p.layer = layer;
        polys.push_back(std::move(p));
    }

    void ConvertEntity(const RawEntity& e, const Xform& xf, int depth,
                       int inheritAci, const std::string& inheritLayer);

    void ConvertList(const std::vector<RawEntity>& list, const Xform& xf,
                     int depth, int inheritAci, const std::string& inheritLayer)
    {
        for (const RawEntity& e : list)
            ConvertEntity(e, xf, depth, inheritAci, inheritLayer);
    }
};

void DxfModel::ConvertEntity(const RawEntity& e, const Xform& xf, int depth,
                             int inheritAci, const std::string& inheritLayer)
{
    if (pointBudget == 0)
        return;

    // gemeinsame Attribute einsammeln
    int aci = 256;
    std::string layer = inheritLayer;
    for (const CodePair& c : e.codes) {
        if (c.code == 62) aci = ToI(c.value);
        else if (c.code == 8) layer = c.value;
    }
    if (aci < 0)
        return; // unsichtbar
    if (aci == 0)
        aci = inheritAci; // ByBlock

    const std::string& t = e.type;

    if (t == "LINE") {
        Pt a, b;
        for (const CodePair& c : e.codes) {
            if (c.code == 10) a.x = ToD(c.value);
            else if (c.code == 20) a.y = ToD(c.value);
            else if (c.code == 11) b.x = ToD(c.value);
            else if (c.code == 21) b.y = ToD(c.value);
        }
        EmitPoly({ a, b }, aci, layer, xf);
    } else if (t == "CIRCLE" || t == "ARC") {
        Pt c0;
        double r = 0, a0 = 0, a1 = 360;
        for (const CodePair& c : e.codes) {
            if (c.code == 10) c0.x = ToD(c.value);
            else if (c.code == 20) c0.y = ToD(c.value);
            else if (c.code == 40) r = ToD(c.value);
            else if (c.code == 50) a0 = ToD(c.value);
            else if (c.code == 51) a1 = ToD(c.value);
        }
        if (r <= 0)
            return;
        std::vector<Pt> pts;
        AddArcSamples(pts, c0.x, c0.y, r, r, 0, a0 * kPi / 180.0, a1 * kPi / 180.0);
        EmitPoly(std::move(pts), aci, layer, xf);
    } else if (t == "ELLIPSE") {
        Pt c0, mj;
        double ratio = 1, t0 = 0, t1 = 2 * kPi;
        for (const CodePair& c : e.codes) {
            if (c.code == 10) c0.x = ToD(c.value);
            else if (c.code == 20) c0.y = ToD(c.value);
            else if (c.code == 11) mj.x = ToD(c.value);
            else if (c.code == 21) mj.y = ToD(c.value);
            else if (c.code == 40) ratio = ToD(c.value);
            else if (c.code == 41) t0 = ToD(c.value);
            else if (c.code == 42) t1 = ToD(c.value);
        }
        const double rx = sqrt(mj.x * mj.x + mj.y * mj.y);
        if (rx <= 0 || ratio <= 0)
            return;
        std::vector<Pt> pts;
        AddArcSamples(pts, c0.x, c0.y, rx, rx * ratio, atan2(mj.y, mj.x), t0, t1);
        EmitPoly(std::move(pts), aci, layer, xf);
    } else if (t == "LWPOLYLINE") {
        std::vector<Pt> verts;
        std::vector<double> bulges;
        bool closed = false;
        double pendingX = 0;
        bool haveX = false;
        for (const CodePair& c : e.codes) {
            if (c.code == 70) closed = (ToI(c.value) & 1) != 0;
            else if (c.code == 10) { pendingX = ToD(c.value); haveX = true; }
            else if (c.code == 20 && haveX) {
                verts.push_back({ pendingX, ToD(c.value) });
                bulges.push_back(0);
                haveX = false;
            } else if (c.code == 42 && !bulges.empty()) {
                bulges.back() = ToD(c.value);
            }
        }
        if (verts.size() < 2)
            return;
        std::vector<Pt> pts;
        pts.push_back(verts[0]);
        for (size_t i = 0; i + 1 < verts.size(); ++i)
            AddBulgeSamples(pts, verts[i], verts[i + 1], bulges[i]);
        if (closed)
            AddBulgeSamples(pts, verts.back(), verts[0], bulges.back());
        EmitPoly(std::move(pts), aci, layer, xf);
    } else if (t == "POLYLINE") {
        // Vertices stecken als eingebettete VERTEX-Codes (im Parser angehaengt)
        std::vector<Pt> verts;
        std::vector<double> bulges;
        bool closed = false;
        Pt cur;
        bool inVertex = false;
        for (const CodePair& c : e.codes) {
            if (c.code == 70 && !inVertex) closed = (ToI(c.value) & 1) != 0;
            else if (c.code == -1) { // Marker: neuer VERTEX
                if (inVertex) {
                    verts.push_back(cur);
                    bulges.push_back(0);
                }
                inVertex = true;
                cur = Pt();
            } else if (inVertex) {
                if (c.code == 10) cur.x = ToD(c.value);
                else if (c.code == 20) cur.y = ToD(c.value);
                else if (c.code == 42 && !bulges.empty()) {
                    // bulge gilt fuer den *vorherigen* fertig gelesenen Vertex?
                    // Nein: fuer den aktuellen -> nach push unten korrigiert
                }
            }
            if (inVertex && c.code == 42) {
                // bulge des aktuellen Vertex vormerken
                // (wird beim push unten uebernommen)
            }
        }
        if (inVertex) {
            verts.push_back(cur);
            bulges.push_back(0);
        }
        // Bulges der POLYLINE-Vertices: zweiter Durchlauf
        {
            size_t vi = static_cast<size_t>(-1);
            for (const CodePair& c : e.codes) {
                if (c.code == -1) ++vi;
                else if (c.code == 42 && vi < bulges.size())
                    bulges[vi] = ToD(c.value);
            }
        }
        if (verts.size() < 2)
            return;
        std::vector<Pt> pts;
        pts.push_back(verts[0]);
        for (size_t i = 0; i + 1 < verts.size(); ++i)
            AddBulgeSamples(pts, verts[i], verts[i + 1], bulges[i]);
        if (closed)
            AddBulgeSamples(pts, verts.back(), verts[0], bulges.back());
        EmitPoly(std::move(pts), aci, layer, xf);
    } else if (t == "SPLINE") {
        int degree = 3;
        std::vector<double> knots;
        std::vector<Pt> ctrl;
        std::vector<Pt> fitPts;
        double px = 0;
        bool haveX = false;
        for (const CodePair& c : e.codes) {
            if (c.code == 71) degree = ToI(c.value);
            else if (c.code == 40) knots.push_back(ToD(c.value));
            else if (c.code == 10) { px = ToD(c.value); haveX = true; }
            else if (c.code == 20 && haveX) { ctrl.push_back({ px, ToD(c.value) }); haveX = false; }
            else if (c.code == 11) { px = ToD(c.value); haveX = true; }
            else if (c.code == 21 && haveX) { fitPts.push_back({ px, ToD(c.value) }); haveX = false; }
        }
        std::vector<Pt> pts;
        if (degree >= 1 && degree <= 7 &&
            ctrl.size() > static_cast<size_t>(degree) &&
            knots.size() == ctrl.size() + degree + 1) {
            const double t0 = knots[degree];
            const double t1 = knots[ctrl.size()];
            const int steps = std::min(static_cast<int>(ctrl.size()) * 8, 512);
            for (int i = 0; i <= steps; ++i) {
                const double tt = t0 + (t1 - t0) * i / steps;
                pts.push_back(DeBoor(degree, ctrl, knots, tt));
            }
        } else if (fitPts.size() >= 2) {
            pts = fitPts; // Fallback: Fit-Punkte als Polylinie
        } else if (ctrl.size() >= 2) {
            pts = ctrl;
        }
        EmitPoly(std::move(pts), aci, layer, xf);
    } else if (t == "POINT") {
        Pt p;
        for (const CodePair& c : e.codes) {
            if (c.code == 10) p.x = ToD(c.value);
            else if (c.code == 20) p.y = ToD(c.value);
        }
        // kleines Kreuz
        const double d = 0.5;
        EmitPoly({ { p.x - d, p.y }, { p.x + d, p.y } }, aci, layer, xf);
        EmitPoly({ { p.x, p.y - d }, { p.x, p.y + d } }, aci, layer, xf);
    } else if (t == "SOLID" || t == "3DFACE" || t == "TRACE") {
        Pt p[4];
        bool has[4] = {};
        for (const CodePair& c : e.codes) {
            int idx = -1;
            if (c.code >= 10 && c.code <= 13) idx = c.code - 10;
            else if (c.code >= 20 && c.code <= 23) idx = c.code - 20;
            if (idx >= 0) {
                if (c.code < 20) p[idx].x = ToD(c.value);
                else p[idx].y = ToD(c.value);
                has[idx] = true;
            }
        }
        if (has[0] && has[1] && has[2]) {
            std::vector<Pt> pts = { p[0], p[1] };
            if (has[3]) { pts.push_back(p[3]); pts.push_back(p[2]); }
            else pts.push_back(p[2]);
            pts.push_back(p[0]);
            EmitPoly(std::move(pts), aci, layer, xf);
        }
    } else if (t == "INSERT") {
        if (depth >= kMaxInsertDepth)
            return;
        std::string name;
        Pt pos;
        double sx = 1, sy = 1, rotDeg = 0;
        for (const CodePair& c : e.codes) {
            if (c.code == 2) name = c.value;
            else if (c.code == 10) pos.x = ToD(c.value);
            else if (c.code == 20) pos.y = ToD(c.value);
            else if (c.code == 41) sx = ToD(c.value);
            else if (c.code == 42) sy = ToD(c.value);
            else if (c.code == 50) rotDeg = ToD(c.value);
        }
        auto it = blocks.find(name);
        if (it == blocks.end())
            return;
        if (sy == 1 && sx != 1)
            sy = sx;

        // Verkettung: erst Block-Transform (lokal), dann bestehende xf
        Xform local;
        local.sx = sx;
        local.sy = sy;
        local.rot = rotDeg * kPi / 180.0;
        Pt base = blockBase.count(name) ? blockBase[name] : Pt();
        // Basis-Punkt des Blocks abziehen, dann positionieren
        local.tx = pos.x - (base.x * local.sx * cos(local.rot) -
                            base.y * local.sy * sin(local.rot));
        local.ty = pos.y - (base.x * local.sx * sin(local.rot) +
                            base.y * local.sy * cos(local.rot));

        // Kombinierte Transformation: xf ∘ local
        Xform combined;
        combined.sx = local.sx; // Naeherung: Skalen multiplizieren sich nur
        combined.sy = local.sy; // korrekt bei rotationsfreiem xf-Anteil -
        combined.rot = local.rot + xf.rot; // fuer Miniaturen ausreichend
        combined.sx *= xf.sx;
        combined.sy *= xf.sy;
        Pt t = xf.Apply({ local.tx, local.ty });
        combined.tx = t.x;
        combined.ty = t.y;

        ConvertList(it->second, combined, depth + 1, aci == 256 ? 7 : aci, layer);
    }
    // alles andere (TEXT, MTEXT, DIMENSION, HATCH, 3DSOLID ...) ueberspringen
}

// Zerlegt den DXF-Text in Gruppencode-Paare und baut Bloecke + Entities auf.
bool ParseDxf(const char* data, size_t len, DxfModel& model)
{
    const char* p = data;
    const char* end = data + len;

    auto readLine = [&](std::string& out) -> bool {
        if (p >= end)
            return false;
        const char* nl = static_cast<const char*>(memchr(p, '\n', end - p));
        const char* lineEnd = nl ? nl : end;
        out = Trim(p, lineEnd);
        p = nl ? nl + 1 : end;
        return true;
    };

    std::string codeLine, valueLine;
    std::string section;
    std::string curBlockName;
    std::string lastLayerName;
    std::vector<RawEntity>* target = nullptr;
    RawEntity* current = nullptr;
    bool inPolyline = false;

    std::vector<RawEntity> entities;

    while (readLine(codeLine)) {
        if (!readLine(valueLine))
            break;
        int code = ToI(codeLine);

        if (code == 0) {
            const std::string& v = valueLine;
            if (v == "SECTION") {
                section.clear();
                current = nullptr;
                continue;
            }
            if (v == "ENDSEC") {
                section.clear();
                target = nullptr;
                current = nullptr;
                inPolyline = false;
                continue;
            }
            if (section == "BLOCKS") {
                if (v == "BLOCK") {
                    curBlockName.clear();
                    current = nullptr;
                    // Name/Basis folgen als Codes; Ziel wird beim Namen gesetzt
                    target = nullptr;
                    // BLOCK selbst als Pseudo-Entity sammeln
                    entities.emplace_back();
                    current = &entities.back();
                    current->type = "__BLOCKHDR";
                    continue;
                }
                if (v == "ENDBLK") {
                    current = nullptr;
                    target = nullptr;
                    inPolyline = false;
                    continue;
                }
            }
            if (section == "ENTITIES" || (section == "BLOCKS" && target)) {
                if (inPolyline) {
                    if (v == "VERTEX") {
                        current->codes.push_back({ -1, "" }); // Vertex-Marker
                        continue;
                    }
                    if (v == "SEQEND") {
                        inPolyline = false;
                        current = nullptr;
                        continue;
                    }
                    inPolyline = false;
                    current = nullptr;
                    // fallthrough: normales neues Entity
                }
                target->emplace_back();
                current = &target->back();
                current->type = v;
                if (v == "POLYLINE")
                    inPolyline = true;
                continue;
            }
            current = nullptr;
            inPolyline = false;
            continue;
        }

        if (code == 2 && section.empty()) {
            section = valueLine;
            if (section == "ENTITIES")
                target = &entities;
            continue;
        }

        if (current) {
            if (current->type == "__BLOCKHDR") {
                if (code == 2 && curBlockName.empty()) {
                    curBlockName = valueLine;
                    target = &model.blocks[curBlockName];
                }
                else if (code == 10) model.blockBase[curBlockName].x = ToD(valueLine);
                else if (code == 20) model.blockBase[curBlockName].y = ToD(valueLine);
                continue;
            }
            if (current->codes.size() < 400000)
                current->codes.push_back({ code, valueLine });
        } else if (section == "TABLES") {
            // LAYER-Farben grob einsammeln (Paarfolge 2=Name ... 62=Farbe)
            if (code == 2) lastLayerName = valueLine;
            else if (code == 62 && !lastLayerName.empty())
                model.layerColor[lastLayerName] = ToI(valueLine);
        }
    }

    // __BLOCKHDR-Pseudo-Entities aus der Entity-Liste entfernen
    entities.erase(std::remove_if(entities.begin(), entities.end(),
                                  [](const RawEntity& e) {
                                      return e.type == "__BLOCKHDR";
                                  }),
                   entities.end());

    Xform identity;
    model.ConvertList(entities, identity, 0, 7, "0");
    return !model.polys.empty();
}

} // namespace

HRESULT RenderDxfToHBitmap(const char* data, size_t len, UINT cx, HBITMAP* phbmp)
{
    if (!phbmp || !data || len == 0 || len > 0x7FFFFFFF || cx == 0)
        return E_INVALIDARG;
    *phbmp = nullptr;

    DxfModel model;
    if (!ParseDxf(data, len, model))
        return E_FAIL;

    // Bounding-Box aus der Geometrie
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (const Poly& poly : model.polys) {
        for (const Pt& p : poly.pts) {
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }
    }
    double w = maxX - minX, h = maxY - minY;
    if (!(w > 0) && !(h > 0))
        return E_FAIL;
    if (w <= 0) w = h * 0.01;
    if (h <= 0) h = w * 0.01;

    // Zielgroesse + 4 % Rand
    UINT dstW, dstH;
    if (w >= h) {
        dstW = cx;
        dstH = std::max(1u, static_cast<UINT>(cx * h / w + 0.5));
    } else {
        dstH = cx;
        dstW = std::max(1u, static_cast<UINT>(cx * w / h + 0.5));
    }
    const double margin = 0.04;
    const double scale = std::min((dstW * (1 - 2 * margin)) / w,
                                  (dstH * (1 - 2 * margin)) / h);
    const double offX = dstW / 2.0 - (minX + w / 2.0) * scale;
    // DXF ist y-nach-oben -> spiegeln
    const double offY = dstH / 2.0 + (minY + h / 2.0) * scale;

    ComPtr<IWICImagingFactory> wic;
    HRESULT hr = GetWicFactory(&wic);
    if (FAILED(hr))
        return hr;

    ComPtr<IWICBitmap> bmp;
    hr = CreateWicRenderBitmap(wic.Get(), dstW, dstH, &bmp);
    if (FAILED(hr))
        return hr;

    ComPtr<ID2D1Factory> d2d;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
    if (FAILED(hr))
        return hr;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1RenderTarget> rt;
    hr = d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt);
    if (FAILED(hr))
        return hr;

    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush);
    if (!brush)
        return E_FAIL;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(D2D1::ColorF::White));

    const float strokeW = std::max(1.0f, cx / 256.0f);
    for (const Poly& poly : model.polys) {
        int aci = poly.aci;
        if (aci == 256) {
            auto it = model.layerColor.find(poly.layer);
            aci = (it != model.layerColor.end() && it->second > 0) ? it->second : 7;
        }
        const COLORREF c = AciToRgb(aci);
        brush->SetColor(D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f,
                                     GetBValue(c) / 255.0f));
        for (size_t i = 0; i + 1 < poly.pts.size(); ++i) {
            const Pt& a = poly.pts[i];
            const Pt& b = poly.pts[i + 1];
            rt->DrawLine(
                D2D1::Point2F(static_cast<float>(a.x * scale + offX),
                              static_cast<float>(offY - a.y * scale)),
                D2D1::Point2F(static_cast<float>(b.x * scale + offX),
                              static_cast<float>(offY - b.y * scale)),
                brush.Get(), strokeW);
        }
    }
    hr = rt->EndDraw();
    if (FAILED(hr))
        return hr;

    return WicSourceToHBitmap(wic.Get(), bmp.Get(), 0, phbmp);
}
