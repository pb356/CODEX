"""Reject accidental third-party runtime and networking dependencies in the shipped PE."""
import re
import sys
from pathlib import Path
imports = set(re.findall(r'DLL Name: (\S+)', Path(sys.argv[1]).read_text()))
allowed = {'KERNEL32.dll', 'USER32.dll', 'GDI32.dll', 'gdiplus.dll', 'SHELL32.dll',
           'ole32.dll', 'ADVAPI32.dll', 'COMDLG32.dll', 'WTSAPI32.dll', 'dwmapi.dll',
           'msvcrt.dll', 'OLEAUT32.dll', 'comctl32.dll', 'SHLWAPI.dll', 'oleacc.dll', 'UxTheme.dll',
           'd2d1.dll', 'WindowsCodecs.dll'}
unknown = {dll for dll in imports if dll.lower() not in {x.lower() for x in allowed}}
if not imports or unknown:
    raise SystemExit(f'Unexpected PE imports: {sorted(unknown)}')
print('PE import check passed; Windows system DLLs only:', ', '.join(sorted(imports)))
