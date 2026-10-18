#pragma once

enum class ERasterizerState : uint8 { SolidBack, SolidNone, SolidFront, Wireframe, ShadowDepth, Count };
enum class EDepthStencilState : uint8 { Default, ReadOnly, Disabled, StencilMask, StencilOutline, ReadOnlyLessEqual, Count };
enum class EBlendState : uint8 { Opaque, AlphaBlend, NoColorWrite, Count };
// ShadowCompare는 그림자 비교용 내부 sampler라 머티리얼 선택 목록에는 나오지 않는다.
enum class ESamplerState : uint8 { LinearClamp, LinearWrap, ShadowCompare, Count };
