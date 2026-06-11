#!/usr/bin/env python3
"""
play_melody.py - Host-side Python driver for the G474 note player mini-synth.

Connects to the board's debug serial port (default COM9 @ 921600).
Enters the interactive note player ('p' from main menu) and plays
pre-defined or custom melodies by sending the same key characters
you would type by hand (1-8 for C major whole tones, space for rest,
+/- for octave, etc.).

This is a lightweight way to play real tunes (e.g. Star Wars intro)
using the CORDIC synth engine over the existing debug link.
No MIDI required for basic use (though UART MIDI input is also planned).

Usage examples:
    python tools/play_melody.py --port COM9 --melody star_wars
    python tools/play_melody.py --list
    python tools/play_melody.py --custom "555 3 6 5 3 6 5" --tempo 0.8

Requirements:
    pip install pyserial

The board should be running the latest firmware with the note player
('p' from the main debug menu). The script will try to unwind to the
main menu with Esc and then enter the player.

Star Wars note: User reported being able to play the intro melody
entirely with the 1-8 whole-tone keys in C major (no sharps/flats).
The example below is a starting point in that spirit; tweak the
sequence and timing to match what sounds right on your keyboard range.
"""

import serial
import time
import argparse
import sys
import re

# Default bench settings (match project smoke/flash etc.)
DEFAULT_PORT = "COM9"
DEFAULT_BAUD = 921600

# Note-player key mapping (C major whole tones via 1-8 as per user)
# 1=C, 2=D, 3=E, 4=F, 5=G, 6=A, 7=B, 8=C (next octave)
# Space = rest/silence (cuts previous tone)
# You can also send '+', '-', '!', '@' etc. for octave control if needed.

# Example melodies. Durations are in seconds (will be scaled by tempo).
# These are simple monophonic sequences suitable for the current player.

def parse_play_string(s, default_octave=4, default_duration=1.0):
    """
    Parse a PLAY meta-language string (preview syntax):
      "CH GH FQ EQ DQ C5Q GH FQ EQ DQ C5H GH FQ EQ FQ DH"
    Returns list of events for play_sequence (mostly (key_char, dur) + occasional __SET_OCT__).

    Syntax (per user update):
      <note|command>{<accidental>}{<octave>}{<duration>{<dot>}}

    Notes: C D E F G A B (upper/lower ok for now). C5 means C in octave 5.
    Accidentals: # or b (basic support).
    Durations (new mapping):
      W = whole (4), H = half (2), Q = quarter (1), I = eighth (0.5)
      X = 16th (0.25), Y = 32nd (0.125) - tentative
    Dot after duration: adds 50% (e.g. CQ. = 1.5 * quarter).
    Default duration = Q if omitted.
    This is a host-side driver for the current terminal note player.
    Future: full on-device interpreter + storage (LittleFS on SPI flash / SD).
    """
    events = []
    # Split on whitespace, ignore separators like -
    tokens = [t.strip() for t in re.split(r'\s+', s) if t.strip() and t.strip() != '-']

    duration_map = {
        'W': 4.0,   # Whole
        'H': 2.0,   # Half
        'Q': 1.0,   # Quarter
        'I': 0.5,   # Eighth (1/8) - E conflicts with note E
        'X': 0.25,  # Sixteenth (1/16) - tentative
        'Y': 0.125, # 1/32 - tentative
    }

    current_octave = default_octave

    for tok in tokens:
        # Match note like C, CH, C5, C5Q, C5Q., FQ, etc.
        m = re.match(r'^([A-Ga-g])([#b]?)([0-9]?)([WHQIXYwhqixy]?)(\.?)$', tok)
        if not m:
            # Could be a rest or command later; for now skip or treat as rest
            if tok.upper() in ('R', 'REST', ' '):
                events.append((' ', default_duration))
            continue

        letter, accidental, oct_str, dur_str, dot = m.groups()
        letter = letter.upper()

        # Octave
        if oct_str:
            target_oct = int(oct_str)
        else:
            target_oct = current_octave

        # Map to player key. We will send octave set command if needed.
        # For simplicity in this driver we send the octave direct key then the note key.
        # Direct octave chars: !@#$%^&* for 1-8
        octave_char_map = {1: '!', 2: '@', 3: '#', 4: '$', 5: '%', 6: '^', 7: '&', 8: '*'}

        # Determine the note key within the octave (1-8 logic)
        # C=1, D=2, E=3, F=4, G=5, A=6, B=7 ; higher C=8 when in lower octave context
        note_to_base_key = {'C': '1', 'D': '2', 'E': '3', 'F': '4', 'G': '5', 'A': '6', 'B': '7'}

        base_key = note_to_base_key.get(letter, '1')

        # Handle accidental by using upper/lower letter keys if the player supports in current octave
        # For preview we mostly stay in naturals; accidentals would use a/A etc.
        if accidental == '#':
            # Map to sharp version: send the upper case letter key if we can express it
            # This driver approximates by using the letter keys relative to current octave.
            # For full accuracy the player would need to be in the right octave already.
            sharp_map = {'C':'C#', 'D':'D#', 'F':'F#', 'G':'G#', 'A':'A#'}  # etc.
            # For now, fall back to sending the natural and note the limitation
            key_to_send = base_key  # user can adjust
        else:
            key_to_send = base_key

        # Duration
        dur_code = dur_str.upper() if dur_str else 'Q'
        dur = duration_map.get(dur_code, default_duration)
        if dot:
            dur *= 1.5

        # Record an "octave set" event + note event.
        # The play_sequence will handle sending the chars.
        # We encode octave set as a special that the player loop understands.
        # For this script we will expand it into actual key sends.
        events.append( ('__SET_OCT__', target_oct, 0.05) )  # small pause for Oct: print
        events.append( (key_to_send, dur) )

        current_octave = target_oct   # track for any future relative logic

    return events


MELODIES = {
    "scale": [
        ('1', 0.4), ('2', 0.4), ('3', 0.4), ('4', 0.4),
        ('5', 0.4), ('6', 0.4), ('7', 0.4), ('8', 0.6),
        (' ', 0.3),
        ('8', 0.4), ('7', 0.4), ('6', 0.4), ('5', 0.4),
        ('4', 0.4), ('3', 0.4), ('2', 0.4), ('1', 0.8),
    ],

    "twinkle": [
        ('1', 0.5), ('1', 0.5), ('5', 0.5), ('5', 0.5),
        ('6', 0.5), ('6', 0.5), ('5', 1.0),
        (' ', 0.2),
        ('4', 0.5), ('4', 0.5), ('3', 0.5), ('3', 0.5),
        ('2', 0.5), ('2', 0.5), ('1', 1.0),
    ],

    # Star Wars intro using the user's new PLAY meta-language preview syntax.
    # This will be parsed by parse_play_string into the right key + duration events.
    "star_wars": "CH GH FQ EQ DQ C5Q GH FQ EQ DQ C5H GH FQ EQ FQ DH",
}

def list_melodies():
    print("Available melodies:")
    for name in sorted(MELODIES.keys()):
        print(f"  {name}")
    print("\nYou can also pass a custom sequence with --custom.")
    print("Example custom (space-separated keys, all same duration):")
    print('  --custom "5 5 5 3 6 5 3 6 5" --duration 0.5')

def enter_player(ser):
    """Try to get to the main debug menu and enter the note player."""
    print("Attempting to enter note player mode...")
    # Send a few Esc to unwind any submenus (safe and matches smoke-test practice)
    for _ in range(3):
        ser.write(b'\x1b')  # ESC
        time.sleep(0.15)

    # Send 'p' to start the interactive note player
    ser.write(b'p')
    time.sleep(1.2)  # Give the board time to print the help banner

    # Optional: flush any banner text the player prints on entry
    ser.reset_input_buffer()
    print("Note player should now be active. Sending melody...")

def play_sequence(ser, sequence, tempo=1.0):
    """
    Send a sequence of events.
    Events can be:
      (key_char, duration)   -- normal note or rest
      ('__SET_OCT__', octave, small_pause) -- from the PLAY parser
    tempo scales durations.
    """
    current_octave = 4
    octave_char_map = {1: '!', 2: '@', 3: '#', 4: '$', 5: '%', 6: '^', 7: '&', 8: '*'}

    for item in sequence:
        if isinstance(item, tuple) and len(item) == 3 and item[0] == '__SET_OCT__':
            _, target_oct, pause = item
            if target_oct != current_octave and target_oct in octave_char_map:
                ser.write(octave_char_map[target_oct].encode('ascii'))
                time.sleep(pause * tempo)
                current_octave = target_oct
            continue

        key, dur = item
        if not key:
            continue
        ser.write(key.encode('ascii'))
        time.sleep(dur * tempo)

    # Final rest to cleanly cut the last note
    ser.write(b' ')
    time.sleep(0.1)

def main():
    parser = argparse.ArgumentParser(
        description="Drive the G474 interactive note player (mini-synth) over serial."
    )
    parser.add_argument("--port", default=DEFAULT_PORT,
                        help=f"Serial port (default {DEFAULT_PORT})")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                        help=f"Baud rate (default {DEFAULT_BAUD})")
    parser.add_argument("--melody", choices=sorted(MELODIES.keys()),
                        help="Play a built-in melody")
    parser.add_argument("--custom", type=str,
                        help="Custom space-separated sequence of keys (e.g. '5 5 5 3 6 5')")
    parser.add_argument("--duration", type=float, default=0.45,
                        help="Duration in seconds for each note in --custom (default 0.45)")
    parser.add_argument("--tempo", type=float, default=1.0,
                        help="Tempo multiplier for built-in melodies (default 1.0)")
    parser.add_argument("--list", action="store_true",
                        help="List available built-in melodies and exit")
    parser.add_argument("--no-enter", action="store_true",
                        help="Assume the note player is already active (skip Esc + 'p')")

    args = parser.parse_args()

    if args.list:
        list_melodies()
        return

    if not args.melody and not args.custom:
        parser.error("Specify --melody NAME or --custom \"keys...\" (or --list)")

    print(f"Opening {args.port} @ {args.baud} ...")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.5)
    except Exception as e:
        print(f"Failed to open serial port: {e}")
        print("Is the board connected? Is TeraTerm or another terminal holding the port?")
        sys.exit(1)

    try:
        if not args.no_enter:
            enter_player(ser)

        if args.melody:
            raw = MELODIES.get(args.melody, "")
            if isinstance(raw, str) and raw:
                # New PLAY meta-language string
                seq = parse_play_string(raw)
                print(f"Playing built-in melody: {args.melody} (parsed PLAY syntax, tempo x{args.tempo})")
                play_sequence(ser, seq, tempo=args.tempo)
            else:
                seq = raw
                print(f"Playing built-in melody: {args.melody} (tempo x{args.tempo})")
                play_sequence(ser, seq, tempo=args.tempo)
        else:
            # Custom: could be old key list or new PLAY string
            custom = args.custom.strip()
            if re.search(r'[A-Ga-g][#b]?[0-9]?[WHQESTwhqest]?', custom):
                seq = parse_play_string(custom)
                print(f"Playing custom PLAY string: {custom}")
                play_sequence(ser, seq, tempo=args.tempo)
            else:
                # old space-separated keys
                keys = custom.split()
                seq = [(k, args.duration) for k in keys]
                print(f"Playing custom key sequence: {keys} (duration {args.duration}s each)")
                play_sequence(ser, seq, tempo=1.0)

        # Clean exit from player
        time.sleep(0.3)
        ser.write(b'\x1b')  # Esc to leave the note player
        print("Done. Player exited (if it was active).")

    finally:
        ser.close()

if __name__ == "__main__":
    main()