# SPDX-License-Identifier: GPL-3.0-or-later
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))


@pytest.fixture(scope="session")
def root():
    return ROOT
