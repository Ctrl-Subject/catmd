# catmd Viewer
Catmd viewer is an application built as an alternative for catmd in the event you
have a terminal that does not accept the graphics produced my catmd

## Current compiler command:
From repo root, compile for your system:

Windows:
``` gcc
g++ -std=c++11 src/Viewer/Viewer.cpp -o src/Viewer/catmd-viewer.exe -lglui -lfreeglut -lopengl32 -lglu32 -lgdiplus -lole32
```

Linux:
``` gcc
    g++ -std=c++11 src/Viewer/Viewer.cpp -o src/Viewer/catmd-viewer -lglui -lglut -lGLU -lGL
```