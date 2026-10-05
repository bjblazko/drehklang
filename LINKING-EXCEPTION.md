# Additional permission for the Bluetooth firmware

The Bluetooth firmware -- the files in `bt/`, `lib/btlink/` and
`lib/btaudio/` -- is licensed under the GNU General Public License,
version 3 or later (see [LICENSE](LICENSE)), with the following additional
permission under section 7 of that licence:

> If you modify this Program, or any covered work, by linking or combining
> it with the binary-only libraries that Espressif Systems distributes with
> ESP-IDF and Arduino-ESP32 (such as the Bluetooth controller
> `libbtdm_app`, `libbtbb`, `libphy` and `libcoexist`), containing parts
> covered by the terms of Espressif's licence for those libraries, the
> licensors of this Program grant you additional permission to convey the
> resulting work. Corresponding Source for a non-source form of such a
> combination shall not include the source code for those libraries.

Why: the Bluetooth firmware cannot run on the ESP32-U4WDH without those
libraries, and their source is not published, so the GPL alone would not
allow passing on a built firmware (ADR 0027). The exception covers only
these files, all written for Drehklang.

It does not cover the main firmware on the ESP32-S3. That links the same
kind of libraries together with ESP32-audioI2S, whose GPL-3.0 code only its
own authors could grant such a permission for. Settling that is part of
the decision before the first binary release (ADR 0026, HE-AAC).
