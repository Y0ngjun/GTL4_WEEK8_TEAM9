#pragma once

#include <format>
#include <functional>
#include "Editor/EditorUI/EditorPanel.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Render/ViewMode.h"

class FViewportsPanel;
class UWorld;
struct ID3D11ShaderResourceView;

enum class EShadowDebugView : int32 {
	Off, Directional, Spot,
	PointPosX, PointNegX, PointPosY, PointNegY, PointPosZ, PointNegZ
};

struct FEditorSettings
{
	// 에디터 렌더 옵션과 편집 Quad 레이아웃의 영구 설정을 한곳에 담는다.
	// Show: 화면에 무엇을 그릴지
	bool bDrawPrimitives = true;
	bool bDrawBoundingBox = false;
	bool bShowUUID = false;
	bool bDrawGrid = true;
	bool bExponentialHeightFog = true;
	bool bVisualizeTileLightCount = false;

	int32 ShadowDebugView = 0;

	// Rendering
	bool bEnableFXAA = true;
	bool bDepthView = false;
	// 끄면 감마 공간 렌더링이 되어 Linear 작업 흐름과 비교할 수 있다 (ColorWorkflow.h)
	bool bLinearWorkflow = true;
	// HDR 씬 컬러를 표시 범위로 압축하는 단계라 Linear 작업 흐름의 Lit 뷰에서만 적용된다
	bool bToneMapping = true;
	// ACES fitted는 중간 회색(0.18)을 어둡게 내리므로, +0.6EV에서 톤매핑 전과 비슷한 밝기가 된다
	float ExposureEV = 0.6f;

	// Grid
	int32 GridSpacing = 1;

	// Camera
	float CameraSpeed = 1.0f;
	float MouseSensitivity = 1.0f;

	// 편집 Quad 분할과 최대화. 기존 ini 키는 저장/복원 함수에서 유지한다.
	float ViewportSplitHorizontal = 0.5f;
	float ViewportSplitVertical = 0.5f;
	bool bViewportMaximized = false;
	int32 MaximizedViewportIndex = 0;
	// 기존 Quad 저장 형식은 4개 슬롯이다. 미저장 값은 초기 카메라 설정을 유지한다.
	float ViewFov[4]{};
	float ViewOrthoWidth[4]{};
	int32 ViewPreset[4]{ -1,-1,-1,-1 };
	// 키가 없으면 FViewRenderMode 기본값(Lit)을 쓴다.
	FViewRenderMode ViewRenderMode[4]{};
	// View별 카메라 Transform은 Quaternion 그대로 저장해 재실행 후 같은 시점을 복원한다.
	// 구형 editor.ini에 키가 없으면 Saved가 false로 남아 뷰포트 클라이언트의 초기 Transform을 유지한다.
	FVector ViewLocation[4]{};
	FQuat ViewRotation[4]{};
	bool bViewLocationSaved[4]{};
	bool bViewRotationSaved[4]{};
};


class FSettingsPanel : public IEditorPanel
{
public:
	FSettingsPanel() = default;
	~FSettingsPanel();

	bool Init() override;
	void Tick(float DeltaTime)override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Settings"; }

	void SetWorld(UWorld* InWorld) { World = InWorld; }

	const FEditorSettings& GetSettings() const { return Settings; }
	// 실행 중 레이아웃 변경을 editor.ini 저장 대상과 동일한 설정 객체에 반영한다.
	FEditorSettings& GetMutableSettings() { return Settings; }
	// 알려진 모든 설정 섹션을 한 번에 다시 써서 editor.ini 값이 서로 덮어쓰이지 않게 저장한다.
	bool SaveSettings() const;
	// editor.ini의 기존 렌더·에디터·뷰포트 값을 같은 설정 객체로 복원한다.
	bool LoadSettings();
	// 초기화된 뷰포트 클라이언트에 저장 설정을 적용한다.
	void SetViewportsPanel(FViewportsPanel* Value);
	void SetHasLitViewQuery(std::function<bool()> InQuery) { HasLitViewQuery = std::move(InQuery); }
	// 종료 저장은 마지막 프레임의 복사본을 사용해 뷰포트 클라이언트 수명과 분리한다.
	void CaptureViewportSettings();

	void SetDirectionalShadowMapQuery(std::function<ID3D11ShaderResourceView* ()> InQuery) { DirectionalShadowMapQuery = std::move(InQuery); }
	void SetSpotShadowMapQuery(std::function<ID3D11ShaderResourceView* ()> InQuery) { SpotShadowMapQuery = std::move(InQuery); }
	void SetPointShadowMapQuery(std::function<ID3D11ShaderResourceView* (uint32)> InQuery) { PointShadowMapQuery = std::move(InQuery); }

private:
	static constexpr float SectionGap = 10.0f;
	static constexpr float SubsectionGap = 4.0f;

	// View별 Transform·투영·표시 설정을 읽어 저장용 스냅샷에 반영한다.
	void ReadViewportSettings(FEditorSettings& Out) const;
	void ApplyViewportSettings();
	FViewportsPanel* ViewportsPanel = nullptr;
	std::function<bool()> HasLitViewQuery;
	UWorld* World;
	FEditorSettings Settings;

	std::function<ID3D11ShaderResourceView* ()> DirectionalShadowMapQuery;
	std::function<ID3D11ShaderResourceView* ()> SpotShadowMapQuery;
	std::function<ID3D11ShaderResourceView* (uint32)> PointShadowMapQuery;
};
