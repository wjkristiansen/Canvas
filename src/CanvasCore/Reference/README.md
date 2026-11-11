# CanvasCore API Reference

The reference for the CanvasCore public API, declared in [`CanvasCore.h`](../../Inc/CanvasCore.h). Each interface has its own page, and the functions are described below. The [CanvasCore spec](../README.md) describes the concepts the API works with, such as worlds, world objects, and threads, and each page links to it rather than repeating it.

## Conventions

- **Interfaces.** A name that begins with `X` is a [GEM interface](../README.md#gem-interface-model). Canvas implements its interfaces, except the ones an app implements, such as the `XLogger` it passes to `CreateCanvas`. A listener, such as `WorldListener`, is a plain struct that the app implements.
- **Results.** A method that can fail returns a `Gem::Result`. Each page lists the results a method returns besides `Gem::Result::Success`.
- **References.** A method that **holds** an object keeps a reference to it. A method that returns a **weak pointer** returns an object without adding a reference, and the pointer is valid while something holds the object. A method that returns an object through an `X**` parameter adds a reference, which the caller releases.
- **Matrices.** Matrices use row vectors, as described in the [root README](../../../README.md).
- **Threads.** Every interface follows the CanvasCore [thread rules](../README.md#threads).

## Interfaces

| Interface                               | Description                     |
|-----------------------------------------|---------------------------------|
| [`XSceneNode`](XSceneNode.md)           | A node in a scene graph         |
| [`XWorld`](XWorld.md)                   | A world and its world objects   |
| [`XWorldObject`](XWorldObject.md)       | A world object                  |


## Functions

### CreateCanvas

```cpp
Gem::Result CreateCanvas(XLogger *pLogger, XCanvas **ppCanvas);
```

Creates a canvas, the object that creates every other CanvasCore object and loads graphics plugins. The canvas holds `pLogger`, which may be `nullptr`, and logs to it from any thread, as the [thread rules](../README.md#threads) describe. `*ppCanvas` receives the canvas.

### Log Functions

```cpp
void LogTrace(XLogger *pLogger, const char *format, ...);
void LogDebug(XLogger *pLogger, const char *format, ...);
void LogInfo(XLogger *pLogger, const char *format, ...);
void LogWarn(XLogger *pLogger, const char *format, ...);
void LogError(XLogger *pLogger, const char *format, ...);
void LogCritical(XLogger *pLogger, const char *format, ...);
```

Each passes a `printf`-style message to `pLogger->Log` at the `LogLevel` it names, and does nothing when `pLogger` is `nullptr`.
