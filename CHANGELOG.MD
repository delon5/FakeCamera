## 1.3

 * One plugin instead of three: "fakecamera.suprx" now includes the BMP image feature. "fakecamerabmp.suprx" and "fakecamerakbmp.suprx" are gone
 * No more dependency on "dsmotion.skprx": the image scrolling reads the tilt from the system motion library (SceMotion). On PS TV it works with any DualShock motion emulator that feeds SceMotion (PSVshell+, ds34motion, DSMotion) and, without one, the image simply stays centered
 * No more dependency on "kuio.skprx": for the titles which cannot read "ux0:" by themselves (Frobisher Says), install ioPlus instead
 * New optional "ux0:data/FakeCamera/config.txt" to disable or invert the tilt scrolling, tune its sensitivity, or write a log file for troubleshooting
 * BMP reading is more robust: 16 and 32 bits files are decoded through their channel masks (so any layout written by GIMP, Photoshop or Paint works, and the alpha of a 32 bits image is opaque unless the file defines one), top-down files (negative height) are accepted, and malformed, paletted or compressed files are rejected instead of producing garbage
 * Fake answers for sceCameraGet/SetImageQuality, sceCameraGet/SetNoiseReduction and sceCameraGet/SetSharpnessOff
 * Build fixed for the current VitaSDK, and a GitHub Actions workflow builds the plugin on every push

## 1.2.1

 * Fix image usage issue when it was named "TITLEID00_Front.bmp" or "TITLEID00_Back.bmp" ("fakecamerabmp.suprx" and "fakecamerakbmp.suprx" only)

## 1.2

 * Add support for "wait next frame" feature (avoid freezing "Frobisher Says")
 * Add new camera format support for BMP loading
 * "fakecamerakbmp.suprx" allows BMP file open with kernel calls (normal calls fail with "Frobisher Says")


## 1.1

 * Fix some wrong resolution values
 * "fakecamerabmp.suprx" allows to load a BMP image in camera buffer (please read the documentation for further information)

 
## 1.0

Initial release
