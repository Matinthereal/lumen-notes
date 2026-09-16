"""Lumen worker processes (D-006): one process per ML concern, talking newline-JSON over a
Unix socket the app opens. Nothing here may ever be imported by the UI thread."""
