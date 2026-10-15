"""Load a small debug font without requiring a Windows font installation."""
import os
from PIL import ImageFont


def debug_font():
    candidates = [os.environ.get('STPS_DEBUG_FONT', ''),
                  'C:/Windows/Fonts/consola.ttf', 'DejaVuSansMono.ttf',
                  '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf']
    for candidate in candidates:
        if not candidate:
            continue
        try:
            return ImageFont.truetype(candidate, 12)
        except OSError:
            pass
    return ImageFont.load_default()
