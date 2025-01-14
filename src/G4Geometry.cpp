#include "G4Geometry.hh"
/* Geant4 interface */
#include "G4Box.hh"
#include "G4LogicalVolume.hh"
#include "G4Material.hh"
#include "G4NistManager.hh"
#include "G4PVPlacement.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VUserDetectorConstruction.hh"
#include "Randomize.hh"
/* Goupil interface */
#include "G4Goupil.hh"

#ifndef M_PI
#define M_PI 3.1415926535897
#endif

static G4LogicalVolume * PlaceInVolume(const std::string& name,
        G4double dim[3], G4Material * material,
        G4RotationMatrix * rot, G4ThreeVector pos,
        G4LogicalVolume * motherVolume);   

DetectorConstruction::DetectorConstruction() {
    this->detectorSize[0] = this->detectorSize[1] = 20.0*CLHEP::m;
    this->detectorSize[2] = 10.0*CLHEP::m;
    
    this->airSize[0] = this->airSize[1] = 2.0*CLHEP::km;
    this->airSize[2] = 1*CLHEP::km;
    this->groundSize[2] = 1*CLHEP::m;
    for(int i = 0; i < 2; i++) {
        this->worldSize[i] = this->groundSize[i] = this->airSize[i];
    }
    this->worldSize[2] = this->groundSize[2] + this->airSize[2];
    this->detectorOffset = 0.5 * (-this->airSize[2] +
            this->detectorSize[2] + this->groundSize[2]) + 5.0 * CLHEP::cm;
    
    /* Normalize intensities */
    double norm = 0.0;
    for (auto pair: this->spectrum) {
        norm += pair.second;
    }
    norm = 1.0 / norm;
    double cdf = 0.0;
    for (auto &pair: this->spectrum) {
        cdf += pair.second * norm;
        pair.second = cdf;
    }
}

DetectorConstruction * DetectorConstruction::Singleton() {
    static DetectorConstruction * detector = nullptr;
    if(detector == nullptr) {
        detector = new DetectorConstruction;
    }
    return detector;
}

G4VPhysicalVolume * DetectorConstruction::Construct() {
    auto manager = G4NistManager::Instance();
    G4LogicalVolume * world = nullptr;
    {
        std::string name = "World";
        auto solid = new G4Box(name, 0.5*this->worldSize[0],
                0.5*this->worldSize[1], 0.5*this->worldSize[2]);
        auto material = manager->FindOrBuildMaterial("G4_AIR");
        world = new G4LogicalVolume(solid, material, name);
    }
    
    G4LogicalVolume * airVolume = nullptr;
    {
        std::string name = "Air";
        auto material = manager->FindOrBuildMaterial("G4_AIR");
        G4ThreeVector pos(0.0, 0.0, 0.5*this->groundSize[2]);
        airVolume = PlaceInVolume(name, this->airSize,
                material, nullptr, pos, world);
    }
    
    {
        std::string name = "Ground";
        auto material = manager->FindOrBuildMaterial("G4_CALCIUM_CARBONATE");
        G4ThreeVector pos(0.0, 0.0, -0.5*this->airSize[2]);
        PlaceInVolume(name, this->groundSize,
                material, nullptr, pos, world);
    }
    
    {
        std::string name = "Detector";
        auto material = manager->FindOrBuildMaterial("G4_AIR");
        G4ThreeVector pos(0.0, 0.0, this->detectorOffset - 0.5*this->groundSize[2]);
        PlaceInVolume(name, this->detectorSize,
                material, nullptr, pos, airVolume);
    }
    
    return new G4PVPlacement(
        nullptr,
        G4ThreeVector(0.0, 0.0, 0.0),
        world,
        "World",
        nullptr,
        false,
        0
    );
}

G4LogicalVolume * PlaceInVolume(const std::string& name,
        G4double dim[3], G4Material * material,
        G4RotationMatrix * rot, G4ThreeVector pos,
        G4LogicalVolume * motherVolume) {
    auto solid = new G4Box(name, 0.5*dim[0], 0.5*dim[1], 0.5*dim[2]);
    auto logicalVolume = new G4LogicalVolume(solid, material, name);
    new G4PVPlacement(
        rot,
        pos,
        logicalVolume,
        name,
        motherVolume,
        false,
        0
    );
    return logicalVolume;
}

void DetectorConstruction::RandomiseState(struct goupil_state * state) {
    const double cosTheta = 2.0 * G4UniformRand() - 1.0;
    const double sinTheta = std::sqrt(1.0 - cosTheta*cosTheta);
    const double phi = 2.0 * M_PI * G4UniformRand();
    const double cosPhi = std::cos(phi);
    const double sinPhi = std::sin(phi);
    
    /* Set momentum direction */
    state->direction.x = sinTheta * cosPhi;
    state->direction.y = sinTheta * sinPhi;
    state->direction.z = cosTheta;
    
    /* Set position */
    G4ThreeVector position(0.0, 0.0, 0.0);
    const auto airOffset = 0.5 * this->groundSize[2];
    for (;;) {
        position[0] = this->airSize[0] * (0.5 - G4UniformRand());
        position[1] = this->airSize[1] * (0.5 - G4UniformRand());
        position[2] = this->airSize[2] * (0.5 - G4UniformRand()) + airOffset;
            
        if ((std::fabs(position[0]) > 0.5 * this->detectorSize[0]) ||
            (std::fabs(position[1]) > 0.5 * this->detectorSize[1]) ||
            (std::fabs(position[2] - this->detectorOffset) > 0.5 * this->detectorSize[2])
        ) {
            break;
        }
    }
    state->position.x = position[0] / CLHEP::cm;
    state->position.y = position[1] / CLHEP::cm;
    state->position.z = position[2] / CLHEP::cm;
    
    /* Set energy */    
    state->energy = this->spectrum.back().first;
    const double u = G4UniformRand();
    for (auto pair: this->spectrum) {
        if (u <= pair.second) {
            state->energy = pair.first;
            break;
        }
    }
}

double DetectorConstruction::RandomiseBackward(
    double alpha, struct goupil_state * state) {
    // Sample face according to respective surfaces.
    double c[3] = { 0.0, 0.0, 0.0 };
    double s = 0.0;
    int axis;
    for (axis = 0; axis < 3; axis++) {
        s += this->detectorSize[(axis + 1) % 3] *
             this->detectorSize[(axis + 2) % 3];
        c[axis] = s;
    }
    const double r = s * G4UniformRand();
    for (axis = 0; axis < 3; axis++) {
        if (r <= c[axis]) break;
    }
    if (axis == 3) axis = 2;
    const double delta = (axis > 0) ? c[axis] - c[axis - 1] : c[0];
    const int dir = ((c[axis] - r) > 0.5 * delta) ? -1 : 1;

    // Sample position.
    double position[3];
    double detectorPosition[3] = { 0.0, 0.0, this->detectorOffset };
    position[axis] = dir * (0.5 * this->detectorSize[axis]
        + 1.0 * CLHEP::um) + detectorPosition[axis];
    for (int i = 0; i < 2; i++) {
        const int ii = (axis + i + 1) % 3;
        position[ii] = this->detectorSize[ii] * (0.5 - G4UniformRand())
            + detectorPosition[ii];
    }
    for (int i = 0; i < 3; i++) {
        position[i] /= CLHEP::cm;
    }
    double w = 2 * c[2] / CLHEP::cm2;

    // Sample direction.
    const double u = G4UniformRand();
    const double cos_theta = sqrt(u);
    const double sin_theta = sqrt(1.0 - u);
    double direction[3];
    const double phi = 2.0 * M_PI * G4UniformRand();
    direction[(axis + 1) % 3] = -dir * sin_theta * cos(phi);
    direction[(axis + 2) % 3] = -dir * sin_theta * sin(phi);
    direction[axis] = -dir * cos_theta;
    w *= M_PI;

    // Sample source energy.  
    double source_energy = this->spectrum.back().first;
    {
        const double zeta = G4UniformRand();
        for (auto pair: this->spectrum) {
            if (zeta <= pair.second) {
                source_energy = pair.first;
                break;
            }
        }
    }

    double energy;
    if (G4UniformRand() < alpha) {
        energy = source_energy;
        w /= alpha;
    } else {
        // Sample state energy.
        const double emin = 1E-02;
        const double lnr = std::log(source_energy / emin);
        energy = emin * std::exp(lnr * G4UniformRand());
        w *= energy * lnr / (1.0 - alpha);
    }
    
    // Set state.
    state->energy = energy;
    state->position.x = position[0];
    state->position.y = position[1];
    state->position.z = position[2];
    state->direction.x = direction[0];
    state->direction.y = direction[1];
    state->direction.z = direction[2];
    state->weight = w;
    
    return source_energy;
}

/* Goupil interface */
const G4VPhysicalVolume * G4Goupil::NewGeometry() {
    /* Build the geometry and return the top "World" volume */
    return DetectorConstruction::Singleton()->Construct();
}

void G4Goupil::DropGeometry(const G4VPhysicalVolume * volume) {
    /* Delete any sub-volume(s) */
    auto && logical = volume->GetLogicalVolume();
    while (logical->GetNoDaughters()) {
        auto daughter = logical->GetDaughter(0);
        logical->RemoveDaughter(daughter);
        G4Goupil::DropGeometry(daughter);
    }
    /* Delete this volume */
    delete logical->GetSolid();
    delete logical;
    delete volume;
}

static void InitialisePrng() {
    // Get a seed from /dev/urandom.
    unsigned long seed;
    FILE * fid = std::fopen("/dev/urandom", "rb");
    fread(&seed, sizeof(long), 1, fid);
    fclose(fid);

    // Initialize the PRNG.
    G4Random::setTheEngine(new CLHEP::MTwistEngine);
    G4Random::setTheSeed(seed);
}


/* Library interface */
extern "C" {
__attribute__((constructor)) static void initialize() {
    InitialisePrng();
}

void g4randomize_states(size_t size, struct goupil_state * states) {
    for (; size > 0; size--, states++) {
        DetectorConstruction::Singleton()->RandomiseState(states);
    }
}

void g4randomize_backward(
    double alpha,
    size_t size,
    struct goupil_state * states,
    double * sources_energies) {
    for (; size > 0; size--, states++, sources_energies++) {
        *sources_energies =
            DetectorConstruction::Singleton()->RandomiseBackward(alpha, states);
    }
}

double g4randomize_source_volume(void) {
    auto airSize = DetectorConstruction::Singleton()->airSize;
    const double airVolume = airSize[0] * airSize[1] * airSize[2];
    auto detSize = DetectorConstruction::Singleton()->detectorSize;
    const double detVolume = detSize[0] * detSize[1] * detSize[2];
    return (airVolume - detVolume) / CLHEP::cm3;
}
}
