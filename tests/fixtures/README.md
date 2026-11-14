# Regression images

These fixtures come from Majoris Photos. The generated RGB gradients
`rgb8.png` and `rgb16.png`, original `vector.svg`, generated
`multipage.tiff`, and generated `multipart.exr` carry the archive's CC0-1.0
fixture licence. `corrupt.bin` deliberately contains no image. The archive
contains no licensed camera RAW samples or its referenced download script.

The framework generates additional fixtures in the build directory,
including an animated GIF and a 200 MP tiled TIFF. Native tests generate
HDR reference images and compare numerical colour and luminance values.
