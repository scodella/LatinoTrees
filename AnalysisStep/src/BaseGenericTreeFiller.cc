// #include "PhysicsTools/TagAndProbe/interface/BaseTreeFiller.h"
#include "LatinoTrees/AnalysisStep/interface/BaseGenericTreeFiller.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"

#include "PhysicsTools/TagAndProbe/interface/ColinsSoperVariables.h"

#include <TList.h>
#include <TObjString.h>

tnp::ProbeVariable::~ProbeVariable() {}

tnp::ProbeFlag::~ProbeFlag() {}

void tnp::ProbeFlag::init(const edm::Event &iEvent) const {
  if (external_) { // take candidates from the token
     edm::Handle<edm::View<reco::Candidate> > view;
     iEvent.getByToken(srcToken_, view);
     if(!view.isValid()) throw cms::Exception("invalid external collection"); 
     passingProbes_.clear();
     for (size_t i = 0, n = view->size(); i < n; ++i) passingProbes_.push_back(view->refAt(i)); // fill the probe particles for making the cuts --> cut has to be applied when the collection is produced
    }
}

void tnp::ProbeFlag::fill(const reco::CandidateBaseRef &probe) const {
    if (external_) {
      value_ = (std::find(passingProbes_.begin(), passingProbes_.end(), probe) != passingProbes_.end()); // if found take the iterator pointer flag 
    } else {
      value_ = bool(cut_(*probe)); // apply the cut on the particle ref
    }
}

tnp::BaseGenericTreeFiller::BaseGenericTreeFiller(const std::string & TreeName, const edm::ParameterSet & iConfig, edm::ConsumesCollector & iC) {

 // make trees as requested
 edm::Service<TFileService> fs;
 tree_ = fs->make<TTree>(TreeName.c_str(),TreeName.c_str()); // make the output file with service

 // add the branches to the three
 addBranches_(tree_, iConfig, iC, ""); 

 // set up weights, if needed
 if (iConfig.existsAs<double>("eventWeight")) {
    weightMode_ = Fixed;
    weight_ = iConfig.getParameter<double>("eventWeight");
 } 
 else if (iConfig.existsAs<edm::InputTag>("eventWeight")) {
     weightMode_ = External;
     weightSrcToken_ = iC.consumes<double>(iConfig.getParameter<edm::InputTag>("eventWeight"));
 } 
 else weightMode_ = None;
    
 if (weightMode_ != None) tree_->Branch("weight", &weight_, "weight/F");
    

 addRunLumiInfo_ = iConfig.existsAs<bool>("addRunLumiInfo") ? iConfig.getParameter<bool>("addRunLumiInfo") : false;
 if (addRunLumiInfo_) {
    tree_->Branch("run",  &run_,  "run/i");
    tree_->Branch("lumi", &lumi_, "lumi/i");
    tree_->Branch("event", &event_, "event/i");
 }
  
 addEventVariablesInfo_ = iConfig.existsAs<bool>("addEventVariablesInfo") ? iConfig.getParameter<bool>("addEventVariablesInfo") : false;
 if (addEventVariablesInfo_) {
 
   recVtxsToken_  = iC.consumes<reco::VertexCollection>(edm::InputTag("offlinePrimaryVertices"));
   beamSpotToken_ = iC.consumes<reco::BeamSpot>(edm::InputTag("offlineBeamSpot"));
   tcmetToken_    = iC.consumes<reco::METCollection>(edm::InputTag("tcMet"));
   pfMetToken_    = iC.consumes<pat::METCollection>(edm::InputTag("pfMet"));
   pfPuppiMetToken_  = iC.consumes<pat::METCollection>(edm::InputTag("pfPuppiMet"));

   tree_->Branch("event_nPV"        ,&mNPV_                 ,"mNPV/I");
   tree_->Branch("event_met_tcmet"    ,&mtcMET_                ,"mtcMET/F");
   tree_->Branch("event_met_tcsumet"  ,&mtcSumET_              ,"mtcSumET/F");
   tree_->Branch("event_met_tcmetsignificance",&mtcMETSign_    ,"mtcMETSign/F");
   tree_->Branch("event_met_pfmet"    ,&mpfMET_                ,"mpfMET/F");
   tree_->Branch("event_met_pfsumet"  ,&mpfSumET_              ,"mpfSumET/F");
   tree_->Branch("event_met_pfmetsignificance",&mpfMETSign_    ,"mpfMETSign/F");

   tree_->Branch("event_met_pfPuppimet"    ,&mPuppiPFMET_                ,"mpfMET/F");
   tree_->Branch("event_met_pfPuppisumet"  ,&mPuppiPFSumET_              ,"mpfSumET/F");
   tree_->Branch("event_met_pfPuppimetsignificance",&mPuppiPFMETSign_    ,"mpfMETSign/F");

   tree_->Branch("event_PrimaryVertex_x"  ,&mPVx_              ,"mPVx/F");
   tree_->Branch("event_PrimaryVertex_y"  ,&mPVy_              ,"mPVy/F");
   tree_->Branch("event_PrimaryVertex_z"  ,&mPVz_              ,"mPVz/F");
   tree_->Branch("event_BeamSpot_x"       ,&mBSx_              ,"mBSx/F");
   tree_->Branch("event_BeamSpot_y"       ,&mBSy_              ,"mBSy/F");
   tree_->Branch("event_BeamSpot_z"       ,&mBSz_              ,"mBSz/F");
 }

 ignoreExceptions_ = iConfig.existsAs<bool>("ignoreExceptions") ? iConfig.getParameter<bool>("ignoreExceptions") : false;
}

tnp::BaseGenericTreeFiller::BaseGenericTreeFiller(BaseGenericTreeFiller & main, const edm::ParameterSet &iConfig, edm::ConsumesCollector && iC, const std::string & branchNamePrefix) :
    addEventVariablesInfo_(false),
    tree_(0){
    addBranches_(main.tree_, iConfig, iC, branchNamePrefix);
}
void tnp::BaseGenericTreeFiller::addBranches_(TTree *tree, const edm::ParameterSet &iConfig, edm::ConsumesCollector & iC, const std::string &branchNamePrefix) {
  // set up variables
  edm::ParameterSet variables  = iConfig.getParameter<edm::ParameterSet>("variables");
  //.. the ones that are strings
  std::vector<std::string> stringVars = variables.getParameterNamesForType<std::string>();

  for (std::vector<std::string>::const_iterator it = stringVars.begin(), ed = stringVars.end(); it != ed; ++it) {

    //---- TLorentzVectors in case v_ is found in the branch prefix --> add probe variable
    if (std::strncmp((branchNamePrefix + *it).c_str(), "v_", strlen("v_")) == 0) {
       vars_.push_back(tnp::ProbeVariable("_TEMP_"+branchNamePrefix + *it+"_x_", variables.getParameter<std::string>(*it)+".x()"));
       vars_.push_back(tnp::ProbeVariable("_TEMP_"+branchNamePrefix + *it+"_y_", variables.getParameter<std::string>(*it)+".y()"));
       vars_.push_back(tnp::ProbeVariable("_TEMP_"+branchNamePrefix + *it+"_z_", variables.getParameter<std::string>(*it)+".z()"));
       vars_.push_back(tnp::ProbeVariable("_TEMP_"+branchNamePrefix + *it+"_t_", variables.getParameter<std::string>(*it)+".t()"));
       vars_.push_back(tnp::ProbeVariable(branchNamePrefix + *it, variables.getParameter<std::string>(*it)));
     }
     //---- std::vector <float>
     else if (std::strncmp((branchNamePrefix + *it).c_str(), "std_vector_", strlen("std_vector_")) == 0){ 
       for (int i=0; i<10; i++) {
	 vars_.push_back(tnp::ProbeVariable("_VECTORTEMP_"+branchNamePrefix + *it+"_"+std::to_string(i+1)+"_", variables.getParameter<std::string>(*it)+"("+std::to_string(i)+")"));
       }
       vars_.push_back(tnp::ProbeVariable(branchNamePrefix + *it, variables.getParameter<std::string>(*it)));        
     }
     //---- simple variable
     else vars_.push_back(tnp::ProbeVariable(branchNamePrefix + *it, variables.getParameter<std::string>(*it)));        
  }

  //.. the ones that are InputTags
  std::vector<std::string> inputTagVars = variables.getParameterNamesForType<edm::InputTag>();
  for (std::vector<std::string>::const_iterator it = inputTagVars.begin(), ed = inputTagVars.end(); it != ed; ++it)
    vars_.push_back(tnp::ProbeVariable(branchNamePrefix + *it, iC.consumes<edm::ValueMap<float> >(variables.getParameter<edm::InputTag>(*it))));
      
  // set up flags
  edm::ParameterSet flags = iConfig.getParameter<edm::ParameterSet>("flags");
  std::vector<std::string> stringFlags = flags.getParameterNamesForType<std::string>();
  for (std::vector<std::string>::const_iterator it = stringFlags.begin(), ed = stringFlags.end(); it != ed; ++it)
    flags_.push_back(tnp::ProbeFlag(branchNamePrefix + *it, flags.getParameter<std::string>(*it)));
    
  //.. the ones that are InputTags
  std::vector<std::string> inputTagFlags = flags.getParameterNamesForType<edm::InputTag>();
  for (std::vector<std::string>::const_iterator it = inputTagFlags.begin(), ed = inputTagFlags.end(); it != ed; ++it) 
    flags_.push_back(tnp::ProbeFlag(branchNamePrefix + *it, iC.consumes<edm::View<reco::Candidate> >(flags.getParameter<edm::InputTag>(*it))));
    

  // then make all the FLOAT and 4D variables in the trees
  for (std::vector<tnp::ProbeVariable>::iterator it = vars_.begin(), ed = vars_.end(); it != ed; ++it) {
   if (std::strncmp(it->name().c_str(), "_TEMP_", strlen("_TEMP_")) != 0 && std::strncmp(it->name().c_str(), "_VECTORTEMP_", strlen("_VECTORTEMP_")) != 0) { 
      //---- only *not* temporary variables  
      //---- TLorentzVectors      
      if (std::strncmp(it->name().c_str(), "v_", strlen("v_")) == 0) 
       tree->Branch(it->name().c_str(), "math::XYZTLorentzVector", it->address4D());      
      //---- std::vector <float>
      else if (std::strncmp(it->name().c_str(), "std_vector_", strlen("std_vector_")) == 0) 
	tree->Branch(it->name().c_str(), "std::vector<float>",it->address_std_vector());
      //---- float
      else tree->Branch(it->name().c_str(), it->address(), (it->name()+"/F").c_str());
      
   }
  }
    
  for (std::vector<tnp::ProbeFlag>::iterator it = flags_.begin(), ed = flags_.end(); it != ed; ++it) 
   tree->Branch(it->name().c_str(), it->address(), (it->name()+"/I").c_str());      
}

tnp::BaseGenericTreeFiller::~BaseGenericTreeFiller() { }


void tnp::BaseGenericTreeFiller::init(const edm::Event &iEvent) const {
    run_   = iEvent.id().run();
    lumi_  = iEvent.id().luminosityBlock();
    event_ = iEvent.id().event();

    for (std::vector<tnp::ProbeVariable>::const_iterator it = vars_.begin(), ed = vars_.end(); it != ed; ++it)
     it->init(iEvent);
    
    for (std::vector<tnp::ProbeFlag>::const_iterator it = flags_.begin(), ed = flags_.end(); it != ed; ++it)
     it->init(iEvent);
    
    if (weightMode_ == External) {
        edm::Handle<double> weight;
        iEvent.getByToken(weightSrcToken_, weight);
        weight_ = *weight;
    }

    if (addEventVariablesInfo_) {

        //////////// Primary vertex //////////////
        edm::Handle<reco::VertexCollection> recVtxs;
        iEvent.getByToken(recVtxsToken_,recVtxs);
        mNPV_ = 0;
        mPVx_ =  100.0;
        mPVy_ =  100.0;
        mPVz_ =  100.0;
        if(recVtxs.isValid()){
         for(unsigned int ind = 0; ind < recVtxs->size(); ind++) {
          if (!((*recVtxs)[ind].isFake()) && ((*recVtxs)[ind].ndof() > 4) && (fabs((*recVtxs)[ind].z()) <= 24.0) && ((*recVtxs)[ind].position().Rho() <= 2.0) ) {
            mNPV_++;
            if(mNPV_==1) { // store the first good primary vertex
              mPVx_ = (*recVtxs)[ind].x();
              mPVy_ = (*recVtxs)[ind].y();
              mPVz_ = (*recVtxs)[ind].z();
            }
          }
	 }	
	}

        //////////// Beam spot //////////////
        edm::Handle<reco::BeamSpot> beamSpot;
        iEvent.getByToken(beamSpotToken_, beamSpot);
        if(beamSpot.isValid()){
         mBSx_ = beamSpot->position().X();
         mBSy_ = beamSpot->position().Y();
         mBSz_ = beamSpot->position().Z();
	}

        /////// TcMET information /////
        edm::Handle<reco::METCollection> tcmet;
        iEvent.getByToken(tcmetToken_, tcmet);
        if(tcmet.isValid()){
         if (tcmet->size() == 0) {
          mtcMET_   = -1;
          mtcSumET_ = -1;
          mtcMETSign_ = -1;
         }
         else {
          mtcMET_   = (*tcmet)[0].et();
          mtcSumET_ = (*tcmet)[0].sumEt();
          mtcMETSign_ = (*tcmet)[0].significance();
         }
	}

        /////// PfMET information /////
        edm::Handle<pat::METCollection> pfmet;
        iEvent.getByToken(pfMetToken_, pfmet);
        if(pfmet.isValid()){
         if (pfmet->size() == 0) {
          mpfMET_   = -1;
          mpfSumET_ = -1;
          mpfMETSign_ = -1;
         }
         else {
          mpfMET_   = (*pfmet)[0].et();
          mpfSumET_ = (*pfmet)[0].sumEt();
          mpfMETSign_ = (*pfmet)[0].significance();
         }
	}

        /////// PfMET Puppi information /////
        edm::Handle<pat::METCollection> pfPuppimet;
        iEvent.getByToken(pfPuppiMetToken_, pfPuppimet);
        if(pfPuppimet.isValid()){
         if (pfPuppimet->size() == 0) {
          mPuppiPFMET_   = -1;
          mPuppiPFSumET_ = -1;
          mPuppiPFMETSign_ = -1;
         }
         else {
          mPuppiPFMET_   = (*pfPuppimet)[0].et();
          mPuppiPFSumET_ = (*pfPuppimet)[0].sumEt();
          mPuppiPFMETSign_ = (*pfPuppimet)[0].significance();
         }
	}

    }
}

void tnp::BaseGenericTreeFiller::fill(const reco::CandidateBaseRef &probe) const {
 for (std::vector<tnp::ProbeVariable>::const_iterator it = vars_.begin(), ed = vars_.end(); it != ed; ++it) {
  if (std::strncmp(it->name().c_str(), "v_", strlen("v_")) != 0 && std::strncmp(it->name().c_str(), "std_vector_", strlen("std_vector_")) != 0) { //---- first normal variables  
    it->fill(probe);
  }
 }
 
 for (std::vector<tnp::ProbeVariable>::const_iterator it = vars_.begin(), ed = vars_.end(); it != ed; ++it) {
  if ( std::strncmp(it->name().c_str(), "_TEMP_", strlen("_TEMP_")) != 0 && std::strncmp(it->name().c_str(), "_VECTORTEMP_", strlen("_VECTORTEMP_")) != 0) { 
   //---- only *not* temporary variables  
   if (std::strncmp(it->name().c_str(), "v_", strlen("v_")) == 0) { //---- now the vectors
    float x = 0;
    float y = 0;
    float z = 0;
    float t = 0;   
    for (std::vector<tnp::ProbeVariable>::const_iterator it2 = vars_.begin(), ed2 = vars_.end(); it2 != ed2; ++it2) {
     if (std::strncmp(it2->name().c_str(), std::string("_TEMP_" + it->name() +"_x_").c_str(), strlen(it2->name().c_str())) == 0) {
      x = it2->internal_value();
     }
     if (std::strncmp(it2->name().c_str(), std::string("_TEMP_" + it->name() +"_y_").c_str(), strlen(it2->name().c_str())) == 0) {
      y = it2->internal_value();
     }
     if (std::strncmp(it2->name().c_str(), std::string("_TEMP_" + it->name() +"_z_").c_str(), strlen(it2->name().c_str())) == 0) {
      z = it2->internal_value();
     }
     if (std::strncmp(it2->name().c_str(), std::string("_TEMP_" + it->name() +"_z_").c_str(), strlen(it2->name().c_str())) == 0) {
      t = it2->internal_value();
     }
    }
    it->fill4D(x,y,z,t);
   }
   else if (std::strncmp(it->name().c_str(), "std_vector_", strlen("std_vector_")) == 0) { //---- now the std::vector <float>
    for (std::vector<tnp::ProbeVariable>::const_iterator it2 = vars_.begin(), ed2 = vars_.end(); it2 != ed2; ++it2) {
     for (int i=0; i<10; i++) {
      if (std::strncmp(it2->name().c_str(), std::string("_VECTORTEMP_" + it->name() +"_"+std::to_string(i+1)+"_").c_str(), strlen(it2->name().c_str())) == 0) {
       it->fillStdVector(it2->internal_value(),i);
      }
     }
    }    
   }
  }
 }
 
 for (std::vector<tnp::ProbeFlag>::const_iterator it = flags_.begin(), ed = flags_.end(); it != ed; ++it) {
  if (ignoreExceptions_)  {
   try{ it->fill(probe); } catch(cms::Exception &ex ){}
  } else {
   it->fill(probe);
  }
 }
 
 if (tree_) tree_->Fill();
}

void tnp::BaseGenericTreeFiller::writeProvenance(const edm::ParameterSet &pset) const {
    TList *list = tree_->GetUserInfo();
    list->Add(new TObjString(pset.dump().c_str()));
}