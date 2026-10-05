.pragma library

var defaultPixelsPerNotch = 64

function delta(event, horizontal, pixelsPerNotch) {
    var pixel = horizontal ? event.pixelDelta.x : event.pixelDelta.y
    if (horizontal && pixel === 0)
        pixel = event.pixelDelta.y
    if (pixel !== 0)
        return pixel

    var angle = horizontal && event.angleDelta.x !== 0
        ? event.angleDelta.x : event.angleDelta.y
    return angle / 120 * (pixelsPerNotch || defaultPixelsPerNotch)
}
