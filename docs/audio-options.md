# Audio Amplifier Options for Tiny Speaker

This document compares audio amplification paths for handheld output.

## Option A - LM386

Pros:
- Common and cheap
- Easy to prototype with through-hole parts

Cons:
- Less efficient than modern class-D
- More susceptible to hiss and supply noise
- Output power is limited at 5V

Typical use:
- Initial analog prototype and validation

## Option B - PAM8302

Pros:
- Class-D efficiency (better battery life)
- Higher usable loudness at 5V for tiny speaker
- Usually cleaner and simpler for handheld use

Cons:
- Requires module sourcing

Typical use:
- Efficient low-cost handheld amplification

## Option C - MAX98357A (I2S Digital Amp + DAC)

Pros:
- Clean digital audio path
- Avoids many analog noise issues

Cons:
- More firmware setup effort
- Module required

Typical use:
- Digital-audio-focused build path

## Power Notes

- USB power bank is a simple battery source option
- Some power banks auto-shut off at low current
- Validate long-run stability under realistic load

## Reference Integration Sequence

1. LM386 for baseline analog validation
2. PAM8302 for efficiency and higher practical loudness
3. MAX98357A for digital-audio-oriented integration
