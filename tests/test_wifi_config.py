import importlib.util
from pathlib import Path
import shutil
import shlex
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).parents[1]
spec=importlib.util.spec_from_file_location('wifi_config',ROOT/'tools/wifi_config.py')
wifi=importlib.util.module_from_spec(spec);spec.loader.exec_module(wifi)


class WifiConfigTests(unittest.TestCase):
    def test_cpp_escaping_and_private_permissions(self):
        ssid='Lab "quoted" \\ ü 📡';password='spaces "quotes" \\ pass'
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'secrets.h';wifi.write_config(path,ssid,password)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode),0o600)
            if not shutil.which('g++'):self.skipTest('g++ required for C++ string round trip')
            source=Path(folder)/'roundtrip.cpp'
            source.write_text('#include "secrets.h"\n#include <stdio.h>\nint main(){puts(WIFI_SSID);puts(WIFI_PASSWORD);}\n')
            exe=Path(folder)/'roundtrip'
            subprocess.run(['g++',str(source),'-o',str(exe)],check=True,capture_output=True)
            self.assertEqual(subprocess.check_output([str(exe)]).decode(),ssid+'\n'+password+'\n')

    def test_invalid_input_preserves_previous_file(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'secrets.h';wifi.write_config(path,'Lab','valid-passphrase');before=path.read_bytes()
            for ssid,password in [('x'*33,''),('ü'*17,''),('Lab\ncode','password'),('Lab','short'),('Lab','x'*64),('','password'),('Lab','invalid\x01password')]:
                with self.subTest(ssid=ssid),self.assertRaises(ValueError):wifi.write_config(path,ssid,password)
                self.assertEqual(path.read_bytes(),before)
            wifi.write_config(path,'Lab','a'*64)
            wifi.write_config(path,'','')

    def test_configure_only_and_eof(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);shutil.copy(ROOT/'install.sh',root/'install.sh')
            (root/'tools').mkdir();shutil.copy(ROOT/'tools/wifi_config.py',root/'tools/wifi_config.py')
            r=subprocess.run(['bash','install.sh','--configure-only'],cwd=root,input='n\n',text=True,capture_output=True)
            self.assertEqual(r.returncode,0,r.stderr)
            path=root/'include/secrets.h';self.assertIn('#define WIFI_SSID ""',path.read_text())
            before=path.read_bytes()
            r=subprocess.run(['bash','install.sh','--configure-only'],cwd=root,input='',text=True,capture_output=True)
            self.assertNotEqual(r.returncode,0)
            self.assertEqual(path.read_bytes(),before)
            self.assertFalse((root/'.venv').exists())

    def test_installer_selects_board_and_port_backs_up_then_declines_upload(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);shutil.copy(ROOT/'install.sh',root/'install.sh')
            wifi.write_config(root/'include/secrets.h','Fixture','private-fixture-pass')
            (root/'.venv/bin').mkdir(parents=True)
            fake=root/'.venv/bin/python'
            fake.write_text('''#!/usr/bin/env bash
set -eu
if (( $# == 1 )) && [[ "$1" == - ]]; then
  cat >/dev/null
  printf '/dev/fixture0 | First board\\n/dev/fixture1 | Second board\\n'
elif [[ "$1" == -m ]]; then
  printf '%s\\n' "$*" >> commands.txt
  if [[ "$2" == esptool ]]; then printf 'fixture original flash' > "${!#}"; fi
else
  exec '''+shlex.quote(sys.executable)+''' "$@"
fi
''');fake.chmod(0o700)
            # Keep settings, choose Tracker and the second port, back up, decline upload.
            r=subprocess.run(['bash','install.sh'],cwd=root,input='y\n2\n2\ny\nn\n',text=True,capture_output=True)
            self.assertEqual(r.returncode,0,r.stderr)
            commands=(root/'commands.txt').read_text()
            self.assertIn('-m platformio run -e tracker\n',commands)
            self.assertIn('-m esptool --port /dev/fixture1 read-flash 0 ALL ',commands)
            self.assertNotIn('upload',commands)
            self.assertNotIn('private-fixture-pass',commands)
            backup=next((root/'.local/backups').rglob('original-flash.bin'))
            self.assertEqual(backup.read_bytes(),b'fixture original flash')
            self.assertEqual(stat.S_IMODE(backup.stat().st_mode),0o600)
            self.assertTrue((backup.parent/'original-flash.sha256').exists())


if __name__=='__main__':unittest.main()
