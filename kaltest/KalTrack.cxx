#include "KalTrack.h"
#include "TKalTrackState.h"
#include "TKalTrackSite.h"
#include "TVTrackHit.h"
#include "TKalDetCradle.h"
#include "TKalTrack.h"         // from KalTrackLib

#include "EXVMeasLayer.h"
#include "EXVTXKalDetector.h"
#include "EXTPCKalDetector.h"
#include "EXVTXHit.h"
#include "EXTPCHit.h"
//#include "EXHYBTrack.h"

#include "gear/GEAR.h"
#include "gear/BField.h"
#include "gear/TPCParameters.h"
#include "gear/PadRowLayout2D.h"

#include "IMPL/TrackImpl.h"
#include "IMPL/TrackerHitImpl.h"
#include "UTIL/Operators.h"
#include "lcio.h"

#include "TObjArray.h"

#include <math.h>
#include <cmath>

#include "streamlog/streamlog.h"




std::ostream& operator<<(std::ostream& o, const KalTrack& trk){

  o << " track: \t" <<  trk._trk->GetEntriesFast() << std::endl ;
  // to be done ....
}


KalTrack::KalTrack(TKalDetCradle* det) : _det( det) {
  _trk = new TKalTrack ;
  _kalHits = new TObjArray ;
}
KalTrack::~KalTrack(){
  delete _trk ;
  delete _kalHits ;
}



void KalTrack::addIPHit(){

  // add a faked Hit for the IP 
    TObject* o =  _det->At( 0 ) ;
  EXTPCMeasLayer* ml = dynamic_cast< EXTPCMeasLayer * >( o ) ;
  
  ml->addIPHit( TVector3( 1.2, 0., 0.) ,   *_kalHits ) ;
}

void KalTrack::addHit( const TVector3& pos, int layer ) {
  
  if( layer >= 0 && ( _det->GetEntries() > layer ) ) {

    TObject* o =  _det->At( layer ) ;

    EXVMeasLayer* ml = dynamic_cast< EXVMeasLayer * >( o ) ;

  if (ml != 0 && ml->IsActive() && dynamic_cast<const EXVKalDetector &>( ml->GetParent(kFALSE) ).IsPowerOn() ) {
    
    ml->ProcessHit( pos, *_kalHits ); // create hit point
    

    if( streamlog_level( DEBUG )  &&  layer % 10 == 0 ){
      //    if(   layer % 10 == 0 ){
      double radius = pos.Perp() ;
      streamlog_out( DEBUG )  << " ***** adding a TPC hit in layer : [" << layer <<  "] at R = " << radius << std::endl ;
    }
    

  } else {
    
    streamlog_out( WARNING ) << " hit not added to KalTrack at : " 
   			     << pos(0) << "," << pos(1) << "," << pos(2)  
 			     << "  in  layer: " << layer
 			     << " ml : " << ml 
 			     << "  ml->IsActive() " <<  ml->IsActive()
 			     << "detector->IsPowerOn() " 
			     << dynamic_cast<const EXVKalDetector &>( ml->GetParent(kFALSE) ).IsPowerOn()
 			     << std::endl ;
  } 
  
} else {
  
  streamlog_out( ERROR ) << " no measurement layer at : " 
			 << pos(0) << "," << pos(1) << "," << pos(2)  
			 << "  in  layer: " << layer 
			 << " detector has  " << _det->GetEntriesFast() << " layers only "
 			 << " - object at given layer : " <<  _det->At( layer )  << std::endl ;
 }
}


void KalTrack::fitTrack(IMPL::TrackImpl* trk) {
  
  //  const Bool_t gkDir = kIterBackward;
  const Bool_t gkDir = kIterForward;
  
  using namespace std ;
  
  // ============================================================
  //  Do Kalman Filter    - copied from EXVKalTrack.cxx
  // ============================================================

  if (_kalHits->GetEntries() < 3) {

    streamlog_out( ERROR)  << "<<<<<< KalTrack::fitTrack(): Shortage of Hits! nhits = " 
			   << _kalHits->GetEntries() << " >>>>>>>" << endl;
    return ;
  }
      
  Int_t i1, i2, i3; // (i1,i2,i3) = (1st,mid,last) hit to filter
  if (gkDir == kIterBackward) {
    i3 = 1;
    i1 = _kalHits->GetEntries() - 1;
    i2 = i1 / 2;
  } else {
    i1 = 1;
    i3 = _kalHits->GetEntries() - 1;
    i2 = i3 / 2;
  }
      
  // ---------------------------
  //  Create a dummy site: sited
  // ---------------------------
      
  TVTrackHit *ht1p = dynamic_cast<TVTrackHit *>(_kalHits->At(i1));
  TVTrackHit *htdp = 0;
  if (dynamic_cast<EXVTXHit *>(ht1p)) {
    htdp = new EXVTXHit(*dynamic_cast<EXVTXHit *>(ht1p));
    //       } else if (dynamic_cast<EXITHit *>(ht1p)) {
    //          htdp = new EXITHit(*dynamic_cast<EXITHit *>(ht1p));
    //       } else if (dynamic_cast<EXITFBHit *>(ht1p)) {
    //          htdp = new EXITFBHit(*dynamic_cast<EXITFBHit *>(ht1p));
  } else if (dynamic_cast<EXTPCHit *>(ht1p)) {
    htdp = new EXTPCHit(*dynamic_cast<EXTPCHit *>(ht1p));
  }
  TVTrackHit &hitd = *htdp;

  hitd(0,1) = 1.e6;   // give a huge error to d
  hitd(1,1) = 1.e6;   // give a huge error to z

  TKalTrackSite &sited = *new TKalTrackSite(hitd);
  sited.SetHitOwner();// site owns hit
  sited.SetOwner();   // site owns states

  // ---------------------------
  //  Create initial helix
  // ---------------------------

  TVTrackHit &h1 = *dynamic_cast<TVTrackHit *>(_kalHits->At(i1)); // first hit
  TVTrackHit &h2 = *dynamic_cast<TVTrackHit *>(_kalHits->At(i2)); // middle hit
  TVTrackHit &h3 = *dynamic_cast<TVTrackHit *>(_kalHits->At(i3)); // last hit
  TVector3    x1 = h1.GetMeasLayer().HitToXv(h1);
  TVector3    x2 = h2.GetMeasLayer().HitToXv(h2);
  TVector3    x3 = h3.GetMeasLayer().HitToXv(h3);

  double Bfield =   h1.GetBfield() ;

  THelicalTrack helstart(x1, x2, x3, h1.GetBfield(), gkDir); // initial helix 

  // ---------------------------
  //  Set dummy state to sited
  // ---------------------------

  static TKalMatrix svd(kSdim,1);
  svd(0,0) = 0.;                        // dr
  svd(1,0) = helstart.GetPhi0();        // phi0
  svd(2,0) = helstart.GetKappa();       // kappa
  svd(3,0) = 0.;                        // dz
  svd(4,0) = helstart.GetTanLambda();   // tan(lambda)
  if (kSdim == 6) svd(5,0) = 0.;        // t0

  static TKalMatrix C(kSdim,kSdim);
  for (Int_t i=0; i<kSdim; i++) {
    C(i,i) = 1.e4;   // dummy error matrix
  }

  sited.Add(new TKalTrackState(svd,C,sited,TVKalSite::kPredicted));
  sited.Add(new TKalTrackState(svd,C,sited,TVKalSite::kFiltered));

  // ---------------------------
  //  Add sited to the kaltrack
  // ---------------------------

  //  EXHYBTrack kaltrack;   // a track is a kal system
  TKalTrack& kaltrack = *_trk ;

  kaltrack.SetOwner();   // kaltrack owns sites
  kaltrack.Add(&sited);  // add the dummy site to this track

  // ---------------------------
  //  Prepare hit iterrator
  // ---------------------------

  TIter next(_kalHits, gkDir); // come in to IP, if gkDir = kIterBackward

  // ---------------------------
  //  Start Kalman Filter
  // ---------------------------

  TVTrackHit *hitp = 0;
  while ((hitp = dynamic_cast<TVTrackHit *>(next()))) {
    TKalTrackSite  &site = *new TKalTrackSite(*hitp); // new site

    if (!kaltrack.AddAndFilter(site)) {               // filter it

      streamlog_out( DEBUG4 )  << "Kaltrack::fitTrack :  site discarded!" << endl;

      delete &site;                        // delete it if failed
    }
  } // end of Kalman filter

  // ---------------------------
  //  Smooth the track
  // ---------------------------
#define SMOOTH_BACK 0
#if SMOOTH_BACK
  Int_t isite = 1;
  //  kaltrack.SmoothBackTo(isite);
  kaltrack.SmoothAll();
  TVKalSite &cursite = static_cast<TVKalSite &>(*kaltrack[isite]);
#else
  TVKalSite &cursite = kaltrack.GetCurSite();
#endif

  // //============== compute crossing points with inner layers ======================

  // TKalTrackSite& theSite =  dynamic_cast<TKalTrackSite&>(  kaltrack.GetCurSite() ) ;

  // int  idx = theSite.GetHit().GetMeasLayer().GetIndex()  ; // index of site from
  
  // -- idx ;
  // while( idx >= 0 ) {

  //   std::auto_ptr<TVTrack> help(& static_cast<TKalTrackState &>( cursite.GetCurState()).CreateTrack() ); // tmp track

  //   TVector3 xx ;                // expected hit position vector
  //   double fid  = 0.;           // deflection angle from the last hit

  //   if (  dynamic_cast<TVSurface *>( _det->At(idx)  )->CalcXingPointWith( *help , xx, fid)  ){

  //     //      streamlog_out( DEBUG )
  // 	std::cout << " ---- crossing layer " << idx <<  " at: " 
  // 	<< xx[0] << ", "  << xx[1] << ", " << xx[2] 
  // 	<< " r: " << xx.Perp()  
  // 	<< std::endl ;
      
  //     --idx ;

  //   } else {

  //     streamlog_out( DEBUG ) << " ---- no crossing point found with layer " << idx << std::endl ;

  //     break ;
  //   }
  // }



  // //===============================================================================


   TVKalState& trkState = cursite.GetCurState() ;

  // dump fit result for now:
  Int_t    ndf  = kaltrack.GetNDF();
  Double_t chi2 = kaltrack.GetChi2();
  Double_t cl   = TMath::Prob(chi2, ndf);

//   Double_t dr   = trkState(0, 0); 
//   Double_t fi0  = trkState(1, 0); 
//   Double_t cpa  = trkState(2, 0);
//   Double_t dz   = trkState(3, 0);
//   Double_t tnl  = trkState(4, 0); 
//   Double_t cs   = tnl/TMath::Sqrt(1.+tnl*tnl);
//   Double_t t0   = trkState(5, 0); 
  
  //============== convert parameters to LCIO convention ====
  
  //  ---- get parameters at origin 

  THelicalTrack helix = ( (TKalTrackState&) trkState ).GetHelix() ;
  double dPhi ;
  helix.MoveTo(  TVector3( 0., 0., 0. ) , dPhi , 0 , 0 ) ;

  double phi       =          helix.GetPhi0()  - M_PI/2. ;
  double omega     =  - .1  / helix.GetRho()  ;              
  double d0        =    10. * helix.GetDrho() ;
  double z0        =    10. * helix.GetDz()   ;
  double tanLambda =  - helix.GetTanLambda()  ;

  //=========================================================

  trk->setD0( d0 ) ;  
  trk->setPhi( phi  ) ; // fi0  - M_PI/2.  ) ;  
  trk->setOmega( omega  ) ;
  trk->setZ0( z0  ) ;  
  trk->setTanLambda( tanLambda ) ;  

  trk->subdetectorHitNumbers().push_back( 1 ) ;  // workaround for bug in lcio::operator<<( Tracks ) - used for picking ....

  trk->setChi2( chi2 ) ;

  float pivot[3] ;
  pivot[0] =  ((TKalTrackSite&) cursite).GetPivot()(0) * 10. ;
  pivot[1] =  ((TKalTrackSite&) cursite).GetPivot()(1) * 10. ;
  pivot[2] =  ((TKalTrackSite&) cursite).GetPivot()(2) * 10. ;

  trk->setReferencePoint( pivot ) ;

  streamlog_out( DEBUG ) << " kaltest track parameters: "
			 << " chi2/ndf " << chi2 / ndf  
    			 << " chi2 " <<  chi2 << std::endl 
			 << " conv. level " << cl  
    			 << "\t D0 " <<  d0 
			 << "\t Phi :" << phi
			 << "\t Omega " <<  omega
			 << "\t Z0 " <<  z0
			 << "\t tan(Lambda) " <<  tanLambda 
			 << "\t pivot : [" << pivot[0] << ", " << pivot[1] << ", "  << pivot[2] 
			 << " - r: " << std::sqrt( pivot[0]*pivot[0]+pivot[1]*pivot[1] ) << "]" 
			 << std::endl ;

  //  streamlog_out( DEBUG ) << kaltrack << std::endl ;


  streamlog_out( DEBUG ) << lcio::header( *trk ) << std::endl ;
  streamlog_out( DEBUG ) << lcio::lcshort( (lcio::Track*)trk ) << std::endl ;



  //#define ADD_LCIO_HITS
#ifdef ADD_LCIO_HITS
  //==========================  add lcio hits to the track for visualization - debugging .... =========================
  int nHit = kaltrack.GetEntriesFast() ;

  for(int i=0 ; i < nHit ; ++i ){
    
    IMPL::TrackerHitImpl* h = new IMPL::TrackerHitImpl ;  //memory leak - only use for debugging ....
    
    TVector3 pv = dynamic_cast<TKalTrackSite &>(*kaltrack[i]).GetPivot() ;

    double pos[3] ;
    pos[0] = pv[0] *10. ;
    pos[1] = pv[1] *10. ;
    pos[2] = pv[2] *10. ;
    
    h->setPosition( pos ) ;

    //    streamlog_out( DEBUG ) << " KalTrack adding hit at : " << pos[0] << ", "  << pos[1] << ", "  << pos[2] << std::endl ;
    
    trk->addHit( h ) ;
    
  }
#endif 


#define ADD_DEBUG_HITS
#ifdef ADD_DEBUG_HITS
  //==========================  add some hits along the helix to the track for visualization - debugging .... =========================
  
  int nHit = 200 ;
  
  double phiStart = 0 ;
  double phiEnd = M_PI/4 ;
  
  double deltaPhi =  ( phiEnd - phiStart ) / nHit ;
  
  if( omega > 0 ) deltaPhi *= -1 ;
  
  for(int i=0 ; i < nHit ; ++i ){
    
    IMPL::TrackerHitImpl* h = new IMPL::TrackerHitImpl ;
    
    TVector3 pv = helix.CalcXAt( phiStart ) ; 
    double pos[3] ;
    pos[0] = pv[0] *10. ;
    pos[1] = pv[1] *10. ;
    pos[2] = pv[2] *10. ;
    
    phiStart += deltaPhi ;
    
    h->setPosition( pos ) ;
    
    trk->addHit( h ) ;
    
  }

#endif

  // ============================================================================================================
}


void KalTrack::getCrossingPoints( PointList& points) {

 
  if( _trk->GetEntriesFast() == 0 )
    return ; // no sites on this track

  // get first and last site used in track fit
  TKalTrackSite& site0 =  *dynamic_cast<TKalTrackSite*>( _trk->At(1)  ) ;
  TKalTrackSite& site1 =  *dynamic_cast<TKalTrackSite*>( _trk->Last() ) ;
  
  int  idx0 = site0.GetHit().GetMeasLayer().GetIndex()  ; 
  int  idx1 = site1.GetHit().GetMeasLayer().GetIndex()  ; 

  streamlog_out( DEBUG ) << " KalTrack::getCrossingPoints : " 
			 << " index at track site[0] : "  <<  idx0  
			 << " index at last site :     "  <<  idx1 
			 <<   std::endl ;
  
  // search inwards first 
  int            idx  =  ( idx1 > idx0  ? idx0  : idx1  ) ; 
  TKalTrackSite& site =  ( idx1 > idx0  ? site0 : site1 ) ; 

  --idx ;  // next site 

  std::auto_ptr<TVTrack> help0(& static_cast<TKalTrackState &>( site.GetCurState()).CreateTrack() ); // tmp track

  while( idx >= 0 ) {

    
    TVector3 xx ;       // expected hit position vector
    double  fid  = 0. ; // deflection angle from the last hit

    if (  dynamic_cast<TVSurface *>( _det->At(idx)  )->CalcXingPointWith( *help0 , xx, fid)  ){
      
      streamlog_out( DEBUG ) << " ---- crossing layer " << idx <<  " at: " 
			     << xx[0] << ", "  << xx[1] << ", " << xx[2] 
			     << " r: " << xx.Perp()  
			     << std::endl ;

      points.push_back( std::make_pair( idx , gear::Vector3D( xx ) ) )  ;

      --idx ;
      
    } else {

      streamlog_out( DEBUG ) << " ---- no crossing point found with layer " << idx << std::endl ;      

      break ;
    }
  }

  // now search outwards
  idx  =  ( idx1 > idx0  ? idx1  : idx0  ) ; 
  site =  ( idx1 > idx0  ? site1 : site0 ) ; 
  
  streamlog_out( DEBUG ) << " ---- creating track for site at layer : " << idx 
			 << " state " << & site.GetCurState()
			 << std::endl ;
  
  std::auto_ptr<TVTrack> help(& static_cast<TKalTrackState &>( site.GetCurState()).CreateTrack() ); // tmp track
  
  //++idx ;  // next site 

  int lastIdx =  _det->GetEntriesFast() ;  // need # of tracking layers

  while( idx < lastIdx ) { 


    TVector3 xx ;       // expected hit position vector
    double  fid  = 0. ; // deflection angle from the last hit

    if (  dynamic_cast<TVSurface *>( _det->At(idx)  )->CalcXingPointWith( *help , xx, fid)  ){
      
      streamlog_out( DEBUG ) << " ---- crossing layer " << idx <<  " at: " 
			     << xx[0] << ", "  << xx[1] << ", " << xx[2] 
			     << " r: " << xx.Perp()  
			     << std::endl ;

      points.push_back( std::make_pair( idx , gear::Vector3D( xx ) ) )  ;

      ++idx ;
      
    } else {

      streamlog_out( DEBUG ) << " ---- no crossing point found with layer " << idx << std::endl ;      

      break ;
    }
  }



}
