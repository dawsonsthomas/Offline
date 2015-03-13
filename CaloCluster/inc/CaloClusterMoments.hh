//
// Original author B.Echenard
//

#ifndef CaloCluster_CaloClusterMoments_HH_
#define CaloCluster_CaloClusterMoments_HH_


// Mu2e includes
#include "RecoDataProducts/inc/CaloCluster.hh"
#include "CalorimeterGeom/inc/Calorimeter.hh"


// C++ includes
#include <iostream>
#include <list>

using CLHEP::Hep3Vector;


namespace mu2e {


    class CaloClusterMoments {


	 public:
             
	     enum cogtype {Linear,Sqrt,Logarithm};
             
	     CaloClusterMoments(Calorimeter const& cal, CaloCluster const& caloCluster, int iSection) : 
	       _cal(cal), _caloCluster(caloCluster),_iSection(iSection),_cog(),_secondMoment(0),_angle(0) 
	     {};

             ~CaloClusterMoments(){};

	                  
	     void calculate(cogtype mode = Linear); 


             CLHEP::Hep3Vector const& cog()     const {return _cog;}
             double            secondMoment()   const {return _secondMoment;}
             double            angle()          const {return _angle;}



	 private:

             Calorimeter const&  _cal;
	     CaloCluster const&  _caloCluster;
	     int                 _iSection;
             double              _offset;
	     
	     CLHEP::Hep3Vector   _cog;
	     double              _secondMoment;
	     double              _angle;
	     
	     
	     
    };


} 

#endif