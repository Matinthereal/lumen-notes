#pragma once
#include <QByteArray>
#include <QVector>
#include "ink/inktypes.h"

// Stroke blob v1 (D-004): "MYNK", u16 version, u32 count; per stroke: u64 id, u8 tool, u32 rgba,
// f32 width, u8 flags, u64 recording_id, u32 start_ms, u32 n, then n points delta-coded as
// zigzag varints of 1/64 px for x/y, u16 pressure, i8 tilt x/y, varint dt_ms. Timestamps survive
// exactly; positions to 1/64 px. Deterministic; round-trip tested.
namespace strokecodec {
QByteArray encode(const QVector<Stroke> &strokes);
bool decode(const QByteArray &blob, QVector<Stroke> &out);
QByteArray encodeOne(const Stroke &s);              // a single stroke (journal records)
bool decodeOne(const QByteArray &bytes, Stroke &s);
constexpr int kVersion = 1;
}
