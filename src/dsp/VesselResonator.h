#pragma once

namespace ocarina {

struct VesselGeometry {
    float compliance;
    float acousticMass;
    float frequency;

    static VesselGeometry from(float volumeCm3, float areaMm2, float neckMm) noexcept;
};

// Pressure and aperture flow in energy-normalized coordinates. Rotation is the
// lossless mass/compliance solution. Jet work and loss are integrated separately.
class VesselResonator {
public:
    void reset() noexcept { pressure = flow = 0; }
    void seed(float strength) noexcept { flow += strength; }
    void configure(float frequency, float loss, float rate) noexcept;
    void step(float jetWork) noexcept;
    float energy() const noexcept { return pressure * pressure + flow * flow; }
    float pressure = 0, flow = 0;

private:
    float cosine = 1, sine = 0, lossPerSample = 0;
};

} // namespace ocarina
