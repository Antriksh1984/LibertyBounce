"""Beat maps for whole GTA III radio stations.

A station file is ~15-60 minutes of different songs, DJ talk, jingles and
adverts, so one global tempo is meaningless. Instead the tempo and beat phase
are measured in short overlapping windows, and every beat carries a strength
(0..1): how much the audio around it sounds like music rather than talk.
Speech has almost no energy below 200 Hz while music has a bass line and
kick drum, so the bass share of the energy tells them apart well (measured on
the III stations: Chatterbox stays under ~0.08, music mostly 0.2-0.5).
"""
import numpy as np
from beat_analysis import onset_envelope

RADIO_ANALYSIS_VERSION = 3
WINDOW_S, HOP_S = 10.0, 2.0
TEMPO_RANGE = (70.0, 190.0)
# The Fourier tempogram tends to lock onto double time for hip-hop (~90 BPM
# read as ~180). Faster tempos are halved: bouncing on every other beat stays
# on the beat and reads better than a frantic double-time wobble.
MAX_BOUNCE_BPM = 145.0
# Bass share of energy that counts as talk / clearly music.
TALK_BASS, MUSIC_BASS = .06, .15

def periodicity(t, weights, bpms):
    """|sum w*e^(i*2pi*t*bpm/60)| / sum w for each candidate tempo, plus the phasors."""
    phasors = np.exp(2j * np.pi * np.outer(bpms / 60, t)) @ weights
    return np.abs(phasors) / max(float(weights.sum()), 1e-12), phasors

def bass_energy(mono, rate):
    """Per ~23 ms frame: (frame start time, bass energy 35-200 Hz, total energy)."""
    size = 256
    frames = mono[:len(mono) // size * size].reshape(-1, size)
    frequencies = np.fft.rfftfreq(size, 1 / rate)
    bass_band = (frequencies >= 35) & (frequencies <= 200)
    bass, total = [], []
    for start in range(0, len(frames), 8192):
        spectrum = np.abs(np.fft.rfft(frames[start:start + 8192] * np.hanning(size), axis=1)) ** 2
        bass.append(spectrum[:, bass_band].sum(axis=1)); total.append(spectrum.sum(axis=1))
    return np.arange(len(frames)) * size / rate, np.concatenate(bass), np.concatenate(total)

def musicness(bass_share):
    return float(np.clip((bass_share - TALK_BASS) / (MUSIC_BASS - TALK_BASS), 0, 1))

def analyze_radio(mono, rate, duration):
    envelope, times, _ = onset_envelope(mono, rate)
    frame_times, bass, total = bass_energy(mono, rate)
    coarse = np.arange(TEMPO_RANGE[0], TEMPO_RANGE[1] + .01, .5)
    beats, strengths, tempos = [], [], []
    previous_tempo = None
    for center in np.arange(HOP_S / 2, duration, HOP_S):
        mask = (times >= center - WINDOW_S / 2) & (times < center + WINDOW_S / 2)
        t, w = times[mask], envelope[mask]
        if len(t) < 16 or w.sum() < 1e-6:
            continue
        scores, _ = periodicity(t, w, coarse)
        best = int(np.argmax(scores))
        # Keep the previous window's tempo (avoids half/double-time flips)
        # unless another tempo is clearly better.
        if previous_tempo is not None:
            near = np.flatnonzero((np.abs(coarse / previous_tempo - 1) < .06) |
                                  (np.abs(coarse / (2 * previous_tempo) - 1) < .06))
            if len(near) and scores[near].max() >= .85 * scores[best]:
                best = int(near[np.argmax(scores[near])])
        if coarse[best] > MAX_BOUNCE_BPM:
            half = np.flatnonzero(np.abs(coarse / (coarse[best] / 2) - 1) < .03)
            if len(half): best = int(half[np.argmax(scores[half])])
        fine = np.arange(coarse[best] - .5, coarse[best] + .501, .05)
        fine_scores, phasors = periodicity(t, w, fine)
        index = int(np.argmax(fine_scores))
        tempo = float(fine[index])
        previous_tempo = tempo
        tempos.append(tempo)
        in_window = (frame_times >= center - WINDOW_S / 2) & (frame_times < center + WINDOW_S / 2)
        weight = musicness(bass[in_window].sum() / max(float(total[in_window].sum()), 1e-12))
        # Beats of this window's grid that fall in its central hop slice.
        period = 60 / tempo
        first = (np.angle(phasors[index]) % (2 * np.pi)) / (2 * np.pi) * period
        k = np.ceil((center - HOP_S / 2 - first) / period)
        beat = first + k * period
        while beat < center + HOP_S / 2:
            if beat >= 0 and (not beats or beat - beats[-1] >= .5 * period):
                beats.append(beat); strengths.append(weight)
            beat += period
    if len(beats) < 2:
        raise ValueError('No beats found in this station.')
    beats_ms = np.round(np.array(beats) * 1000, 4)
    strengths = np.array(strengths)
    return dict(mode='radio-windows', analysis_version=RADIO_ANALYSIS_VERSION,
                beat_count=len(beats_ms), median_bpm=float(np.median(tempos)),
                strong_fraction=float(np.mean(strengths >= .5)),
                beats_ms=beats_ms.tolist(), strengths=np.round(strengths, 3).tolist())
