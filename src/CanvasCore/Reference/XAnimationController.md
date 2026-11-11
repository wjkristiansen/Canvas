# XAnimationController

The playback of a model's animation clips in one instance of the model. A controller animates its instance's nodes and skeleton instances, its **targets**, and holds its own playback state: the active clip, the time in it, and any cross-fade. The clips' keyframes are shared by every instance of the model, so instances of one model animate independently, each at its own point in its own clip. A controller is not a scene element: it has no bounds and is bound to no node.

Inherits `XCanvasElement`. Created by [`XCanvas::CreateWorldObject`](XCanvas.md#createworldobject) for an instance of a model that has animation clips, and held by the instance root, whose [`XSceneNode::GetAnimationController`](XSceneNode.md#getanimationcontroller) returns it. The controller animates the nodes of that instance, wherever the instance root is.

## Clips

### GetClipCount, GetClipName, FindClip

```cpp
uint32_t GetClipCount();
const char *GetClipName(uint32_t index);
uint32_t FindClip(const char *name);
```

`GetClipCount` returns the number of the model's clips, indexed from 0. `GetClipName` returns the name of the clip at `index`, or `nullptr` when `index` is out of range. `FindClip` returns the index of the first clip named `name`, or `InvalidClipIndex` when there is none.

## Playback

### Play, PlayByName

```cpp
Gem::Result Play(uint32_t clipIndex, float blendDuration);
Gem::Result PlayByName(const char *clipName, float blendDuration);
```

Make the clip at `clipIndex`, or the first clip named `clipName`, the active clip, playing from time 0. When `blendDuration` is greater than 0, the targets cross-fade from their current values to the clip's over `blendDuration` seconds; otherwise the clip applies at once, from the next `Update`. Clips loop.

| Result       | When                                                                   |
|--------------|------------------------------------------------------------------------|
| `InvalidArg` | `clipIndex` is out of range, or no clip is named `clipName`            |

### Stop

```cpp
void Stop();
```

Stops playback, leaving the targets at their current values and ending any cross-fade. The active clip and time are unchanged.

### ResetToBindPose

```cpp
void ResetToBindPose();
```

Stops playback and sets every target to its value at instantiation.

### Update

```cpp
Gem::Result Update(float dtime);
```

While a clip plays, advances its time by `dtime` seconds, wrapping at the clip's duration, and writes the clip's values, or the cross-faded values, onto the targets. [`XWorld::Update`](XWorld.md#update) calls it for each node of an active world object that holds a controller, before it updates the elements bound to that node.

### GetActiveClipIndex, GetTime, SetTime, IsPlaying

```cpp
uint32_t GetActiveClipIndex();
float GetTime();
void SetTime(float seconds);
bool IsPlaying();
```

`GetActiveClipIndex` returns the index of the active clip, 0 before any clip plays. `GetTime` and `SetTime` get and set the time in the active clip, in seconds; the next `Update` advances from it. `IsPlaying` returns whether a clip plays.

## Targets

### GetTargetCount, GetTargetValue

```cpp
uint32_t GetTargetCount();
Gem::Result GetTargetValue(uint32_t targetIndex, float *pValue, uint32_t count);
```

`GetTargetCount` returns the number of the model's animation targets, indexed from 0. `GetTargetValue` writes the value of the target at `targetIndex` to `pValue`: the value the controller last applied, or the target's value at instantiation before then, as `count` floats, as many as the target's value type has.

| Result       | When                                                            |
|--------------|-----------------------------------------------------------------|
| `BadPointer` | `pValue` is `nullptr`                                           |
| `OutOfRange` | `targetIndex` is out of range                                   |
| `InvalidArg` | `count` is not the number of floats the target's value type has |
