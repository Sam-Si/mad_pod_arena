#pragma once
// Race progress helpers (SSOT PR-4b). Arena terminal conditions use csb::Game fields.

namespace csb_progress {

inline int GlobalNext(int lap, int local_cp, int track_size) {
    if (track_size <= 0) return local_cp;
    return lap * track_size + local_cp;
}

// Decode global index into lap and local CP in [0, track_size).
inline void Decode(int global_next, int track_size, int* lap, int* local_cp) {
    if (track_size <= 0) {
        if (lap) *lap = 0;
        if (local_cp) *local_cp = global_next;
        return;
    }
    if (lap) *lap = global_next / track_size;
    if (local_cp) *local_cp = global_next % track_size;
}

inline int LocalNext(int global_next, int track_size) {
    if (track_size <= 0) return global_next;
    return global_next % track_size;
}

}  // namespace csb_progress
