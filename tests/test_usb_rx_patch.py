"""Exercise the pinned CDC pause function and F1 allocation macro at queue-full."""
import hashlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from usb_rx_patch import EXPECTED_SHA256, NEEDLE, patch_cdc_source

CORE = Path.home() / ".platformio" / "packages" / "framework-arduinoststm32"
CDC = CORE / "libraries" / "USBDevice" / "src" / "cdc" / "usbd_cdc.c"
HAL = CORE / "system" / "Drivers" / "STM32F1xx_HAL_Driver" / "Inc" / "stm32f1xx_hal_pcd.h"


class UsbRxPatchTests(unittest.TestCase):
    def test_exact_pinned_source(self):
        self.assertEqual(hashlib.sha256(CDC.read_bytes()).hexdigest(), EXPECTED_SHA256)
        patched = patch_cdc_source(CDC.read_bytes())
        self.assertIn(b"defined(STM32F1xx)", patched)
        self.assertIn(b"CDC_resume_receive()", patched)

    def test_unreviewed_source_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "source changed"):
            patch_cdc_source(CDC.read_bytes() + b"\n")

    def test_missing_or_altered_pause_call_is_rejected(self):
        with self.assertRaises(ValueError):
            patch_cdc_source(CDC.read_bytes().replace(NEEDLE, b""))

    def compile_pause_model(self, source, family, expected_prepare):
        compiler = shutil.which("g++")
        if not compiler:
            self.skipTest("g++ required for the actual-core pause regression")
        text = source.decode("utf-8")
        begin = text.index("#ifdef USE_USBD_COMPOSITE\nuint8_t USBD_CDC_ClearBuffer")
        end = text.index("#endif /* USBD_USE_CDC */", begin)
        method = text[begin:end]
        hal = HAL.read_text()
        macros = hal[hal.index("#define PCD_CALC_BLK32"):hal.index("#define PCD_SET_EP_RX_DBUF0_CNT")]
        model = r'''
#include <stdint.h>
#include <stddef.h>
#define USB_CNTRX_BLSIZE 0x8000U
#define USBD_OK 0
#define USBD_FAIL 1
#define CDC_OUT_EP 1
struct USBD_HandleTypeDef { void *pClassDataCmsit[1]; unsigned classId; };
static unsigned prepared=0,capacity=64;static bool valid=false;
''' + macros + r'''
int USBD_LL_PrepareReceive(USBD_HandleTypeDef *,unsigned,void *,unsigned size){
    uint16_t raw=0;PCD_SET_EP_CNT_RX_REG(&raw,size);
    capacity=raw&0x8000 ? 32*((raw>>10&31)+1) : 2*(raw>>10&31);
    valid=true;++prepared;return 0;
}
''' + method + r'''
int main(){
    USBD_HandleTypeDef device={{reinterpret_cast<void *>(1)},0};
    // Completed OUT leaves RX in NAK; no full-packet queue reservation exists.
    if(USBD_CDC_ClearBuffer(&device)!=USBD_OK)return 1;
    if(prepared!=EXPECTED_PREPARE)return 2;
    if(EXPECTED_PREPARE==0){
        if(valid)return 3; // Next64-byte packet must NAK, not enter an undersized buffer.
        USBD_LL_PrepareReceive(&device,1,reinterpret_cast<void *>(1),64);
        if(!valid||capacity!=64)return 4; // Consumer frees space; full-packet RX resumes.
    }else{
        if(!valid||capacity!=32)return 5; // Actual F1 HAL turns length0 into32.
    }
    device.pClassDataCmsit[0]=nullptr;
    if(USBD_CDC_ClearBuffer(&device)!=USBD_FAIL)return 6;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp, exe = Path(folder) / "pause.cpp", Path(folder) / "pause.exe"
            cpp.write_text(model)
            defines = ["-DUSBD_CDC_USE_SINGLE_BUFFER", f"-DEXPECTED_PREPARE={expected_prepare}"]
            if family:
                defines.append("-DSTM32F1xx")
            subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", *defines,
                            str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=5)

    def test_queue_full_leaves_nak_and_resume_accepts_full_packet(self):
        self.compile_pause_model(patch_cdc_source(CDC.read_bytes()), True, 0)

    def test_original_bug_and_other_family_branch_remain_explicit(self):
        self.compile_pause_model(CDC.read_bytes(), True, 1)
        self.compile_pause_model(patch_cdc_source(CDC.read_bytes()), False, 1)


if __name__ == "__main__":
    unittest.main()
