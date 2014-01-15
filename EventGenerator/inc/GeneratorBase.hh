#ifndef EventGenerator_GeneratorBase_hh
#define EventGenerator_GeneratorBase_hh
//
// Base class to allow generic access to all of the event generator classes.
// Also provides access to the random number engine associated with the
// EventGenerator module.
//
// $Id: GeneratorBase.hh,v 1.9 2014/01/15 17:07:30 tassiell Exp $
// $Author: tassiell $
// $Date: 2014/01/15 17:07:30 $
//
// Original author Rob Kutschke
//

// Framework includes
#include "art/Framework/Services/Optional/RandomNumberGenerator.h"

// Mu2e includes
#include "MCDataProducts/inc/GenParticleCollection.hh"

// CLHEP includes
#include "CLHEP/Random/RandomEngine.h"

namespace mu2e {

  class GeneratorBase{

  public:
    GeneratorBase( bool isFromG4bl=false ) : _isFromG4bl(isFromG4bl) {
    }
    virtual ~GeneratorBase(){
    }

    virtual void generate( GenParticleCollection&  ) = 0;

    bool isFromG4bl() { return _isFromG4bl; }

  protected:

    // A helper function to access the random number engine associated with this module.
    static art::RandomNumberGenerator::base_engine_t&
    getEngine( const art::RandomNumberGenerator::label_t& engine_label = "" );
    bool _isFromG4bl;

};

} // end namespace mu2e,

#endif /* EventGenerator_GeneratorBase_hh */

