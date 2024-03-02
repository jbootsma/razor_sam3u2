# Audio Board Design

## High-level

What it does:

- Get audio from ADC or USB input
- Volume visualizer on LEDs
- Play audio back out USB output

Bonus features if time/resources:

- Histogram on LCD (is it fast enough?)
  - Could do a mode switch and show in LEDs
  - Enough time to FFT? Might have to do some sort of time-slicing.
- Approximate audio through buzzer(s).
- Guitar Tuning Mode
  - LCD backlight: Red = flat, blue = sharp, green = in tune.
  - Display: Detected Note + fine-tuner.
  - Option to adjust reference frequency?

Other Notes:

- Cap sample rate to 48 KHz. Internal buffers can then be sized to 50 Samples.
- Internal audio format is normalized 16-bit signed integer.
- Do everything mono initially. Stereo support could be a bonus feature if there's enough processing power.
- Not dropping a frame is pretty important. Add some additional timing capture to main loop to get an idea of how much processing is actually being used.

## Architecture

- Initially thought of breaking up USB audio from other items as separate apps.
- Design of USB audio class makes this difficult, most of the settings directly apply to processing code.
- Instead create usb_audio_utils.(h|c) for easily doing USB audio things, but app directly implements the class.
  - This is similar to how the standard USB descriptor support was done.

- Program flow: For each 1ms frame:
  1. Check for and apply any configuration changes.
     1. Volume
     2. Sample rate
     3. Source selection
     4. Output enable
  2. Acquire audio input buffer.
     1. Look at source selection.
     2. Acquire samples.
     3. If source not ready/active then may need to pad with 0's.
        - Return code of sample query can indicate source wasn't ready.
        - For ADC, initial frame may be short some samples (and due to ADC bugs have some bad values). Therefore the initial 2 frames should be discarded and the ADC indicate it is not enabled.
        - For USB there is no initial frame problem, but it won't be ready until the proper interface alternate is selected.
  3. Internal processing:
     1. FFT step / volume analysis.
     2. Update LEDs/Display.
  4. Audio output:
     1. If output not enabled, skip.
     2. Apply volume output control / mute to samples (straight multiplication).
        - Changes to effective volume/mute should happen at 0-crossings.
     3. Write samples to USB endpoint.

### USB Audio Driver

- Details on what Windows supports [here](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers)

- Device descriptor must be:
  - Class = 0xEF
  - SubClass = 0x02
  - Protocol = 0x01

- Two functions
  - One for audio out, one for audio in.
- Each function needs a control interface and a streaming interface.
- Is possible to represent a streaming interface for the ADC audio source as well, if sample-rate/volume control is desired.
- Control and streaming interfaces are related using interface association descriptors (IAD).
- Audio function class (for use in IAD):
  - Class code is AUDIO (0x01)
  - Subclass is set to UNDEFINED (0x00)
  - Protocol is set to IP_VERSION_02_00 (0x20)

- For interfaces:
  - Class code is AUDIO (0x01)
  - SubClass chooses control (0x01) or streaming (0x02)
  - Protocol is same as the function's.

- Use Async audio for output
- Use sync audio for input

- Components each have their own descriptor
  - Specified as class-specific descriptors on the control interface.
- Components of the function:
  - Two input terminals
  - One (sample) clock source: Configurable clock rate.
  - One output terminal
  - Selector for the two input terminals
  - Feature unit before output terminal for master volume control / mute

- Control interface:
  - Uses default control endpoint for controlling all component settings.
  - Optional Interrupt endpoint for communicating changes in settings. (Not needed).
- Streaming interface:
  - Must have default 0-bandwidth setting.
  - Alternate settings for different bandwidths.
  - If bandwidth is exceeded, switch to 0 and report through control-interface interrupt.
  - (Maybe just provide a single setting that supports max sample rate to be easy).

## Implementation tasks

- [X] Program skeleton
- [X] usb_audio_utils.h defines from spec.
- [X] Output silence, fixed frequency (44100 KHz), no volume control
- [X] Fixed rate triangle wave (441 Hz).
- [X] Volume + Mute
- [X] Sample rate control
- [ ] Loopback input
- [X] Source selection
- [ ] Volume visualization
- [ ] ADC input
  - [ ] Pre-amp volume control?
