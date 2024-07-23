#include "pch.h"
#include "FastAerialTrainer.h"
#include "bakkesmod/wrappers/Engine/WorldInfoWrapper.h"
#include <sstream>


BAKKESMOD_PLUGIN(FastAerialTrainer, "FastAerialTrainer", plugin_version, PLUGINTYPE_FREEPLAY)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

float FastAerialTrainer::GetCurrentTime() {
	ServerWrapper server = gameWrapper->GetCurrentGameState();
	if (!server) return 0;
	WorldInfoWrapper world = server.GetWorldInfo();
	if (!world) return 0;
	return world.GetTimeSeconds();
}

void FastAerialTrainer::onLoad()
{
	_globalCvarManager = cvarManager;

	gameWrapper->RegisterDrawable(std::bind(&FastAerialTrainer::RenderCanvas, this, std::placeholders::_1));

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.SetVehicleInput", std::bind(&FastAerialTrainer::OnTick, this, std::placeholders::_1));

	//when car uses first jump
	gameWrapper->HookEventWithCaller<CarWrapper>("Function CarComponent_Jump_TA.Active.BeginState",
		[this](CarWrapper car, void* params, std::string eventname)
		{
			float now = GetCurrentTime();

			holdFirstJumpStartTime = now;
			HoldingFirstJump = true;

			totalJumpTime = 0;
			HoldingJoystickBackDuration = 0;
			checkHoldingJoystickBack = true;
			pitchHistory.clear();
			lastTickTime = now;
		});

	gameWrapper->HookEventWithCaller<CarWrapper>("Function TAGame.Car_TA.OnJumpReleased",
		[this](CarWrapper car, void* params, std::string eventname)
		{
			if (HoldingFirstJump)
			{
				HoldingFirstJump = false;
				holdFirstJumpStopTime = GetCurrentTime();
			}
		});

	//when car uses double jump
	gameWrapper->HookEventWithCaller<CarWrapper>("Function CarComponent_DoubleJump_TA.Active.BeginState",
		[this](CarWrapper caller, void* params, std::string eventname)
		{
			// Only register the double jump if we didn't loose our flip or landed in between.
			if (checkHoldingJoystickBack) {
				DoubleJumpPressedTime = GetCurrentTime();
				totalJumpTime = DoubleJumpPressedTime - holdFirstJumpStartTime;
			}

			checkHoldingJoystickBack = false;
		});
}

void FastAerialTrainer::OnTick(CarWrapper car)
{
	float now = GetCurrentTime();

	if (HoldingFirstJump)
		holdFirstJumpDuration = now - holdFirstJumpStartTime;
	else
		holdFirstJumpDuration = holdFirstJumpStopTime - holdFirstJumpStartTime;

	if (HoldingFirstJump)
		TimeBetweenFirstAndDoubleJump = 0;
	else if (DoubleJumpPressedTime > holdFirstJumpStopTime)
		TimeBetweenFirstAndDoubleJump = DoubleJumpPressedTime - holdFirstJumpStopTime;
	else if (checkHoldingJoystickBack)
		TimeBetweenFirstAndDoubleJump = now - holdFirstJumpStopTime;
	else
		TimeBetweenFirstAndDoubleJump = 0;

	// We either landed or have to land for another double jump. No need to record things further.
	if (!car.HasFlip() || (car.IsOnGround() && !car.GetbJumped()))
		checkHoldingJoystickBack = false;

	ControllerInput inputs = car.GetInput();
	if (checkHoldingJoystickBack)
	{
		float sensitivity = gameWrapper->GetSettings().GetGamepadSettings().AirControlSensitivity;
		float intensity = std::min(1.f, sensitivity * inputs.Pitch);
		float duration = now - lastTickTime;
		HoldingJoystickBackDuration += intensity * duration;
		lastTickTime = now;

		pitchHistory.push_back(intensity);
	}
}

static std::string toPrecision(float x, int precision) {
	std::stringstream stream;
	stream << std::fixed << std::setprecision(precision) << x;
	return stream.str();
}

void FastAerialTrainer::RenderCanvas(CanvasWrapper canvas)
{
	if (gameWrapper->IsPaused())
		return;

	DrawBar(
		canvas, "Hold First Jump in ms: ", holdFirstJumpDuration * 1000, JumpDuration_HighestValue,
		GuiPosition, BarSize(),
		GuiColorBackground, JumpDuration_RangeList
	);

	DrawBar(
		canvas, "Time to Double Jump in ms: ", TimeBetweenFirstAndDoubleJump * 1000, DoubleJumpDuration_HighestValue,
		GuiPosition + Offset(), BarSize(),
		GuiColorBackground, DoubleJumpDuration_RangeList
	);

	canvas.SetColor(255, 255, 255, 255);
	canvas.SetPosition(GuiPosition + (Offset() * 2));
	float JoystickBackDurationPercentage = !totalJumpTime ? 0.f : 100.f * HoldingJoystickBackDuration / totalJumpTime;
	canvas.DrawString("Tilt Between Jumps: " + toPrecision(JoystickBackDurationPercentage, 1) + "%", FontSize(), FontSize());

	if (GuiDrawPitchHistory)
		DrawPitchHistory(canvas);
}

void FastAerialTrainer::DrawBar(
	CanvasWrapper& canvas, std::string text, float value, float maxValue,
	Vector2 barPos, Vector2 barSize,
	LinearColor backgroundColor, std::vector<Range>& colorRanges
)
{
	// Draw background
	canvas.SetPosition(barPos);
	canvas.SetColor(backgroundColor);
	canvas.FillBox(barSize);
	for (Range& range : colorRanges)
	{
		LinearColor preview = *range.color;
		preview.A *= GuiColorPreviewOpacity;
		canvas.SetColor(preview);
		float left = std::min(range.min / maxValue, 1.f);
		float width = std::min((range.max - range.min) / maxValue, 1.f - left);
		canvas.SetPosition(barPos + Vector2{ (int)(left * barSize.X), 0 });
		canvas.FillBox(Vector2{ (int)(width * barSize.X), barSize.Y });
	}

	// Draw colored bar
	canvas.SetPosition(barPos);
	for (Range& range : colorRanges)
	{
		if (range.min <= value && value <= range.max)
		{
			canvas.SetColor(*range.color);
		}
	}
	float result = std::min(1.f, value / maxValue);
	canvas.FillBox(Vector2{ (int)(barSize.X * result), barSize.Y });

	// Draw border
	canvas.SetColor(255, 255, 255, 255);
	canvas.SetPosition(barPos);
	canvas.DrawBox(barSize);
	for (Range& range : colorRanges)
	{
		canvas.SetColor(255, 255, 255, 255);
		float result = range.max / maxValue;
		if (result < 1.f) {
			canvas.SetPosition(barPos + Vector2{ (int)(result * barSize.X), 0 });
			canvas.FillBox(Vector2{ 2, barSize.Y });
		}
	}

	// Draw text
	canvas.SetColor(255, 255, 255, 255);
	canvas.SetPosition(barPos + Vector2{ 0, barSize.Y });
	canvas.DrawString(text + std::to_string(value), FontSize(), FontSize());
}

void FastAerialTrainer::DrawPitchHistory(CanvasWrapper& canvas)
{
	Vector2F offset = (Vector2F() + Offset()) * 2.6f + GuiPosition;
	canvas.SetColor(255, 255, 255, 127);
	float boxPadding = GuiSize / 200.f;
	canvas.SetPosition(offset - boxPadding);
	canvas.DrawBox(Vector2{ GuiSize, GuiSize / 10 } + 2 * boxPadding);

	canvas.SetColor(255, 255, 255, 255);
	int i = 0;
	int size = pitchHistory.size();
	float prevPitch;
	for (float currentPitch : pitchHistory)
	{
		if (i > 0 && currentPitch >= 0 && prevPitch >= 0)
		{
			Vector2F start = { (float)(i - 1) / size * GuiSize, (1 - prevPitch) * GuiSize / 10 };
			Vector2F end = { (float)i / size * GuiSize, (1 - currentPitch) * GuiSize / 10 };
			float width = GuiSize / 500.f;
			canvas.DrawLine(start + offset, end + offset, width);
		}
		prevPitch = currentPitch;
		i++;
	}
}

void FastAerialTrainer::onUnload()
{
}