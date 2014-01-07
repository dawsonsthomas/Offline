// Andrei Gaponenko, 2013

#include <iostream>
#include <string>
#include <cmath>
#include <memory>
#include <algorithm>

#include "cetlib/exception.h"

#include "CLHEP/Vector/ThreeVector.h"
#include "CLHEP/Vector/LorentzVector.h"
#include "CLHEP/Random/RandomEngine.h"
#include "CLHEP/Random/RandFlat.h"
#include "CLHEP/Random/RandGeneral.h"
#include "CLHEP/Units/PhysicalConstants.h"

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Services/Optional/TFileService.h"

#include "ConfigTools/inc/ConfigFileLookupPolicy.hh"
#include "SeedService/inc/SeedService.hh"
#include "ConditionsService/inc/GlobalConstantsHandle.hh"
#include "ConditionsService/inc/ParticleDataTable.hh"
#include "MCDataProducts/inc/PDGCode.hh"
#include "MCDataProducts/inc/GenParticle.hh"
#include "MCDataProducts/inc/GenParticleCollection.hh"
#include "MCDataProducts/inc/EventWeight.hh"
#include "Mu2eUtilities/inc/RandomUnitSphere.hh"
#include "Mu2eUtilities/inc/CzarneckiSpectrum.hh"
#include "Mu2eUtilities/inc/SimpleSpectrum.hh"
#include "Mu2eUtilities/inc/EjectedProtonSpectrum.hh"
#include "Mu2eUtilities/inc/BinnedSpectrum.hh"
#include "Mu2eUtilities/inc/Table.hh"

#include "TTree.h"
#include "TFile.h"

namespace mu2e {

  //================================================================
  namespace {
    struct InputStop {
      float x;
      float y;
      float z;
      float t;
      float tauNormalized; // proper time normalized to lifetime

      InputStop() : x(), y(), z(), t(), tauNormalized() {}
    };
  } // namespace {}

  //================================================================
  class StoppedParticleReactionGun : public art::EDProducer {
    std::vector<std::string> inputFiles_;
    std::string treeName_;
    long averageStopsToUseLimit_;
    double stopUseFraction_;

    bool computeWeightFromProperTime_;

    fhicl::ParameterSet psphys_;

    PDGCode::type pdgId_;
    double mass_;

    enum SpectrumVar { TOTAL_ENERGY, KINETIC_ENERY };
    SpectrumVar spectrumVariable_;
    static SpectrumVar parseSpectrumVar(const std::string& name);

    double elow_; // BinnedSpectrum does not store emin and emax reliably
    double ehi_;
    BinnedSpectrum spectrum_;
    static BinnedSpectrum parseSpectrumShape(const fhicl::ParameterSet& psphys,
                                             PDGCode::type pdgId,
                                             double *elow,
                                             double *ehi);

    int verbosityLevel_;

    art::RandomNumberGenerator::base_engine_t& eng_;
    CLHEP::RandFlat randFlat_;
    CLHEP::RandGeneral randSpectrum_;
    RandomUnitSphere randomUnitSphere_;

    std::vector<InputStop> mustops_;
    long countInputStops();
    void loadInputFiles();
    void addStoppedMuons(TFile* infile);
    double generateEnergy();

  public:
    explicit StoppedParticleReactionGun(const fhicl::ParameterSet& pset);
    virtual void produce(art::Event& event);
    virtual void beginRun(art::Run& run);
  };

  //================================================================
  StoppedParticleReactionGun::StoppedParticleReactionGun(const fhicl::ParameterSet& pset)
    : inputFiles_(pset.get<std::vector<std::string> >("inputFiles"))
    , treeName_(pset.get<std::string>("treeName"))
    , averageStopsToUseLimit_(pset.get<long>("averageStopsToUseLimit", 0))
    , stopUseFraction_(1.)
    , computeWeightFromProperTime_(pset.get<bool>("computeWeightFromProperTime", false))
    , psphys_(pset.get<fhicl::ParameterSet>("physics"))
    , pdgId_(PDGCode::type(psphys_.get<int>("pdgId")))
    , mass_(GlobalConstantsHandle<ParticleDataTable>()->particle(pdgId_).ref().mass().value())
    , spectrumVariable_(parseSpectrumVar(psphys_.get<std::string>("spectrumVariable")))
    , elow_()
    , ehi_()
    , spectrum_(parseSpectrumShape(psphys_, pdgId_, &elow_, &ehi_))
    , verbosityLevel_(pset.get<int>("verbosityLevel", 0))
    , eng_(createEngine(art::ServiceHandle<SeedService>()->getSeed()))
    , randFlat_(eng_)
    , randSpectrum_(eng_, spectrum_.getPDF(), spectrum_.getNbins())
    , randomUnitSphere_(eng_)
  {
    produces<mu2e::GenParticleCollection>();
    if(computeWeightFromProperTime_) {
      produces<mu2e::EventWeight>();
    }

    if(inputFiles_.empty()) {
      throw cet::exception("BADCONFIG")<<"Error: no inputFiles";
    }

    if(verbosityLevel_ > 0) {
      std::cout<<"StoppedParticleReactionGun: producing particle "
               <<pdgId_
               <<", mass = "<<mass_
               <<std::endl;
    }
  }

  //================================================================
  StoppedParticleReactionGun::SpectrumVar
  StoppedParticleReactionGun::parseSpectrumVar(const std::string& name) {
    if(name == "totalEnergy")  return TOTAL_ENERGY;
    if(name == "kineticEnergy")  return KINETIC_ENERY;
    throw cet::exception("BADCONFIG")<<"StoppedParticleReactionGun: unknown spectrum variable "<<name<<"\n";
  }

  //================================================================
  BinnedSpectrum
  StoppedParticleReactionGun::parseSpectrumShape(const fhicl::ParameterSet& psphys,
                                                 PDGCode::type pdgId,
                                                 double *elow,
                                                 double *ehi)
  {
    BinnedSpectrum res;

    const std::string spectrumShape(psphys.get<std::string>("spectrumShape"));
    if (spectrumShape == "Czarnecki") {
      const double mass(GlobalConstantsHandle<ParticleDataTable>()->particle(pdgId).ref().mass().value());
      *elow = psphys.get<double>("elow", mass);
      *ehi = GlobalConstantsHandle<PhysicsParams>()->getEndpointEnergy();
      res.initialize< CzarneckiSpectrum >(*elow, *ehi, psphys.get<double>("spectrumResolution"));
    }
    else if (spectrumShape == "flat") {
      *elow = psphys.get<double>("elow");
      *ehi = psphys.get<double>("ehi");
      res.initialize<SimpleSpectrum>(*elow, *ehi, *ehi-*elow, SimpleSpectrum::Spectrum::Flat );
    }
    else if (spectrumShape == "conversion") {
      // A simple kludge: ignore the random distribution by setting elow=ehi=eConversion
      res.initialize<SimpleSpectrum>(0., 1., 1., SimpleSpectrum::Spectrum::Flat );
      *elow = *ehi = GlobalConstantsHandle<PhysicsParams>()->getEndpointEnergy();
    }
    else if (spectrumShape == "ejectedProtons") {
      *elow = 0.;
      *ehi = 105.; // cut off at muon mass
      double spectrumRes = (*ehi - *elow)/psphys.get<unsigned>("nbins");
      res.initialize<EjectedProtonSpectrum>(*elow, *ehi, spectrumRes);
    }
    else if (spectrumShape == "tabulated") {
      res.initialize(loadTable<2>(psphys.get<std::string>("spectrumFileName")));
      *elow = res.getAbscissa(0);
      *ehi = res.getAbscissa(res.getNbins()-1);
    }
    else {
      throw cet::exception("BADCONFIG")
        << "StoppedParticleReactionGun: unknown spectrum shape "<<spectrumShape<<"\n";
    }

    return res;
  }

  //================================================================
  void StoppedParticleReactionGun::beginRun(art::Run& run) {
    loadInputFiles();

    if(verbosityLevel_ > 0) {
      std::cout<<"StoppedParticleReactionGun: using = "
               <<mustops_.size()
               <<" stopped particles"
               <<std::endl;
    }
  }

  //================================================================
  long StoppedParticleReactionGun::countInputStops() {
    long res=0;
    art::ServiceHandle<art::TFileService> tfs;
    for(const auto& fn : inputFiles_) {
      const std::string resolvedFileName = ConfigFileLookupPolicy()(fn);
      TFile *infile = tfs->make<TFile>(resolvedFileName.c_str(), "READ");
      TTree *nt = dynamic_cast<TTree*>(infile->Get(treeName_.c_str()));
      if(!nt) {
        throw cet::exception("BADINPUT")<<"Could not get tree \""<<treeName_
                                        <<"\" from file \""<<infile->GetName()
                                        <<"\"\n";
      }

      const Long64_t nTreeEntries = nt->GetEntries();
      if(verbosityLevel_ > 1) {
        std::cout<<"StoppedParticleReactionGun: found "<<nTreeEntries<<" stops in input file "<<resolvedFileName<<std::endl;
      }

      res += nTreeEntries;
    }

    return res;
  }

  //================================================================
  void StoppedParticleReactionGun::loadInputFiles() {
    art::ServiceHandle<art::TFileService> tfs;

    if(averageStopsToUseLimit_) {
      const long totalStops = countInputStops();
      if(averageStopsToUseLimit_ < totalStops) {
        stopUseFraction_ = averageStopsToUseLimit_/double(totalStops);
        if(verbosityLevel_ > 0) {
          std::cout<<"StoppedParticleReactionGun: stopUseFraction = "<<stopUseFraction_
                   <<" for the requested limit = "<<averageStopsToUseLimit_
                   <<" and total number of input stops = "<<totalStops
                   <<std::endl;
        }
      }
      else {
        if(verbosityLevel_ > 0) {
          std::cout<<"StoppedParticleReactionGun: stopUseFraction = "<<1
                   <<" for the requested limit = "<<averageStopsToUseLimit_
                   <<" and total number of input stops = "<<totalStops
                   <<std::endl;
        }
      }
    }

    for(const auto& fn : inputFiles_) {
      const std::string resolvedFileName = ConfigFileLookupPolicy()(fn);
      TFile *infile = tfs->make<TFile>(resolvedFileName.c_str(), "READ");
      addStoppedMuons(infile);
    }

  } // loadInputFiles()

  //================================================================
  void StoppedParticleReactionGun::addStoppedMuons(TFile* infile) {
    TTree *nt = dynamic_cast<TTree*>(infile->Get(treeName_.c_str()));
    if(!nt) {
      throw cet::exception("BADINPUT")<<"Could not get tree \""<<treeName_
                                      <<"\" from file \""<<infile->GetName()
                                      <<"\"\n";
    }

    const Long64_t nTreeEntries = nt->GetEntries();
    if(verbosityLevel_ > 0) {
      std::cout<<"StoppedParticleReactionGun: reading "<<nTreeEntries
               <<" entries.  Tree "<<treeName_
               <<", file "<<infile->GetName()
               <<std::endl;
    }

    InputStop mu;
    TBranch *bmu = nt->GetBranch("stops");
    bmu->SetAddress(&mu);

    mustops_.reserve(mustops_.size()
                     // Add "mean + 3sigma": do not re-allocate in most cases.
                     + stopUseFraction_*nTreeEntries
                     + 3*stopUseFraction_*sqrt(double(nTreeEntries)));

    for(Long64_t i=0; i<nTreeEntries; ++i) {
      bmu->GetEntry(i);

      if(!averageStopsToUseLimit_ || (randFlat_.fire() < stopUseFraction_)) {
        mustops_.emplace_back(mu);
      }

    }
  }

  //================================================================
  void StoppedParticleReactionGun::produce(art::Event& event) {

    std::unique_ptr<GenParticleCollection> output(new GenParticleCollection);

    const InputStop& stop = mustops_.at(randFlat_.fireInt(mustops_.size()));

    const CLHEP::Hep3Vector pos(stop.x, stop.y, stop.z);

    const double energy = generateEnergy();
    const double p = energy * sqrt(1 - std::pow(mass_/energy,2));

    CLHEP::Hep3Vector p3 = randomUnitSphere_.fire(p);
    CLHEP::HepLorentzVector fourmom(p3, energy);

    output->emplace_back(pdgId_,
                         GenId::StoppedParticleReactionGun,
                         pos,
                         fourmom,
                         stop.t);

    event.put(std::move(output));

    if(computeWeightFromProperTime_) {
      const double weight = exp(-stop.tauNormalized);
      std::unique_ptr<EventWeight> pw(new EventWeight(weight));
      event.put(std::move(pw));
    }
  }

  //================================================================
  double StoppedParticleReactionGun::generateEnergy() {
    double res = elow_ + (ehi_ - elow_)*randSpectrum_.fire();
    switch(spectrumVariable_) {
    case TOTAL_ENERGY: break;
    case KINETIC_ENERY: res += mass_; break;
    }
    return res;
  }

  //================================================================
} // namespace mu2e

DEFINE_ART_MODULE(mu2e::StoppedParticleReactionGun);
