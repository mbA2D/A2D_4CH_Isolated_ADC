"""Pinned STM32F1 single-buffer USB backpressure fix; shared framework stays untouched."""
import hashlib

EXPECTED_SHA256 = "74452d43c3ed6209b6ab313e1cc25495a1ce5af31c35cf0bed161bb7b95a1a61"
NEEDLE = b"    USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, 0, 0);"
REPLACEMENT = b"""#if defined(STM32F1xx) && defined(USBD_CDC_USE_SINGLE_BUFFER)
    /* The completed OUT packet already leaves RX in NAK. Keep it paused until
     * CDC_resume_receive() has room for a full packet. On F103, arming length0
     * actually allocates32 bytes as VALID, so the next64-byte packet STALLs. */
#else
    USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, 0, 0);
#endif"""


def patch_cdc_source(source):
    if hashlib.sha256(source).hexdigest() != EXPECTED_SHA256 or source.count(NEEDLE) != 1:
        raise ValueError("USB CDC source changed; review the pinned F1 backpressure patch before building")
    return source.replace(NEEDLE, REPLACEMENT)
