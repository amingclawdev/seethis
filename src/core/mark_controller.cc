#include "core/reference.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace seethis::core {
namespace {
double DistanceToSegment(Point2D p,Point2D a,Point2D b) {
  const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
  const double t=length>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.0,1.0):0;
  return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
bool HitPath(const Context& context,const std::vector<DisplayPoint>& path,
             DisplayPoint point,double radius,
             const std::vector<FrozenDisplay>& displays) {
  if(!DisplayTopologyMatches(context.displays,displays))return false;
  for(std::size_t index=0;index<path.size();++index) {
    const auto& sample=path[index];
    if(sample.display_id!=point.display_id||sample.backing_scale!=point.backing_scale)continue;
    const auto& previous=index>0&&path[index-1].display_id==sample.display_id?
        path[index-1]:sample;
    if(DistanceToSegment(point.position,previous.position,sample.position)<=radius)return true;
  }return false;
}
bool HitRegions(const Reference& reference,DisplayPoint point,double radius,
                const std::vector<FrozenDisplay>& displays) {
  for(const auto& region:EffectiveRegions(reference)) {
    bool hit = false;
    ForEachSubpath(region, [&](const auto& path) {
      if (HitPath(reference.context,path,point,radius,displays)) hit = true;
    });
    if (hit) return true;
  }
  return false;
}
ReferenceMark LightweightMark(const Reference& source) {
  ReferenceMark mark{source};mark.value.source.bytes.clear();
  mark.value.crop.bytes.clear();return mark;
}
bool VisibleJob(const ReferenceJobSnapshot& job,
                const WindowObservation& window_observation,
                const PageObservation& page_observation) {
  return ReferenceVisible(job.context,job.page_identity,window_observation,
                          page_observation);
}
}  // namespace

struct MarkController::Transaction {
  std::uint64_t generation=0;
  Reference reference;
  std::optional<CaptureResult> capture;
  std::string capture_error;
  Stamp began;
  std::int64_t key_down_monotonic_ms=0;
  std::int64_t key_up_monotonic_ms=0;
  std::uint64_t copy_generation=0;
  std::optional<std::int64_t> pasteboard_change_count;
  bool released=false;
  bool submitted=false;
};

MarkController::MarkController(ReferenceStore& store,Clipboard clipboard)
    :store_(store),clipboard_(std::move(clipboard)),
     dispatcher_([](std::function<void()> task){task();}) {}

MarkController::MarkController(ReferenceStore& store,
    PendingClipboard pending_clipboard,ReadyClipboard ready_clipboard,
    Dispatcher dispatcher)
    :store_(store),pending_clipboard_(std::move(pending_clipboard)),
     ready_clipboard_(std::move(ready_clipboard)),dispatcher_(std::move(dispatcher)) {}

MarkController::~MarkController(){lifetime_.reset();}

std::uint64_t MarkController::Begin(std::string id,double crop_margin,
                                    std::int64_t key_down_monotonic_ms) {
  if(current_)Abort("superseded drawing attempt");
  ++generation_;current_=std::make_unique<Transaction>();
  current_->generation=generation_;current_->reference.id=id;
  current_->reference.mark_id=std::move(id);current_->reference.crop_margin=crop_margin;
  current_->began=Now();current_->key_down_monotonic_ms=key_down_monotonic_ms;
  phase_=ReferencePhase::kContextPending;copy_result_=CopyResult::kNone;
  last_copy_id_.clear();
  status_="Capturing original window…";return generation_;
}

bool MarkController::Current(std::uint64_t generation) const {
  return current_&&current_->generation==generation;
}

bool MarkController::Pending() const {return current_!=nullptr;}

bool MarkController::BindContext(std::uint64_t generation,Context context) {
  if(!Current(generation)||current_->reference.context.pid!=0)return false;
  if(context.pid<=0||context.pid!=context.window_pid||context.pid==context.self_pid||
     context.window_id==0||context.displays.empty()) {
    current_->capture_error="context_unavailable";status_="Original window context unavailable";
    return false;
  }
  if(context.bundle_id=="com.google.Chrome")context.window_title.clear();
  current_->reference.context=std::move(context);phase_=ReferencePhase::kCapturePending;
  status_="Draw around the reference";return true;
}

bool MarkController::BindPageIdentity(std::uint64_t generation,
                                      PageIdentity identity) {
  if (!Current(generation) || current_->reference.page_identity) return false;
  if (!ValidPageIdentity(identity) ||
      !PageIdentityMatchesContext(identity,current_->reference.context)) {
    status_="Chrome page identity unavailable";
    return false;
  }
  current_->reference.page_identity=std::move(identity);
  if(current_->released)AcceptReleased();
  return true;
}

void MarkController::Captured(std::uint64_t generation,CaptureResult result) {
  Transaction* transaction=Current(generation)?current_.get():nullptr;
  if(!transaction)for(auto& accepted:accepted_)
    if(accepted->generation==generation){transaction=accepted.get();break;}
  if(!transaction||transaction->capture||transaction->submitted)return;
  if(!result.error.empty()||!ValidPixels(result.pixels))
    transaction->capture_error=result.error.empty()?"capture_invalid":"capture_failed";
  else transaction->capture=std::move(result);
  if(transaction->released)SubmitIfReady(generation);
}

void MarkController::Release(std::uint64_t generation,
    std::vector<std::vector<DisplayPoint>> regions,Stamp time,
    std::int64_t key_up_monotonic_ms) {
  if(!Current(generation)||current_->released)return;
  current_->reference.schema=2;
  current_->reference.capture_api="ScreenCaptureKit.SCScreenshotManager.selected-display";
  current_->reference.provenance="historical;context-before-overlay;single-selected-display;pixels-at-async-completion;multi-region;no-semantic-object-id";
  current_->reference.path.clear();
  current_->reference.subpath_version = 0;
  current_->reference.regions.reserve(regions.size());
  for(auto& path:regions)current_->reference.regions.push_back({std::move(path),{}, {}, {}});
  ReleasePrepared(time,key_up_monotonic_ms);
}

void MarkController::Release(
    std::uint64_t generation,
    std::vector<InteractionSnapshot::LogicalRegion> regions, Stamp time,
    std::int64_t key_up_monotonic_ms) {
  if(!Current(generation)||current_->released)return;
  current_->reference.schema=2;
  current_->reference.capture_api="ScreenCaptureKit.SCScreenshotManager.selected-display";
  current_->reference.provenance="historical;context-before-overlay;single-selected-display;pixels-at-async-completion;multi-region;no-semantic-object-id";
  current_->reference.path.clear();
  current_->reference.subpath_version = 1;
  current_->reference.regions.reserve(regions.size());
  for(auto& region:regions)
    current_->reference.regions.push_back({{}, {}, {}, std::move(region)});
  ReleasePrepared(time,key_up_monotonic_ms);
}

void MarkController::Release(std::uint64_t generation,
    std::vector<DisplayPoint> path,Stamp time,
    std::int64_t key_up_monotonic_ms) {
  if(!Current(generation)||current_->released)return;
  if (current_->reference.page_identity ||
      current_->reference.context.bundle_id=="com.google.Chrome") {
    current_->reference.schema=2;
    current_->reference.capture_api="ScreenCaptureKit.SCScreenshotManager.selected-display";
    current_->reference.provenance="historical;context-before-overlay;single-selected-display;pixels-at-async-completion;multi-region;no-semantic-object-id";
    current_->reference.path.clear();
    current_->reference.subpath_version=0;
    current_->reference.regions={{std::move(path),{}, {}, {}}};
  } else {
    current_->reference.schema=1;current_->reference.path=std::move(path);
    current_->reference.regions.clear();
  }
  ReleasePrepared(time,key_up_monotonic_ms);
}

void MarkController::ReleasePrepared(Stamp time,
    std::int64_t key_up_monotonic_ms) {
  current_->released=true;
  current_->reference.circle_completed=time;
  current_->key_up_monotonic_ms=key_up_monotonic_ms;
  if(current_->reference.context.bundle_id=="com.google.Chrome"&&
     !current_->reference.page_identity) {
    phase_=ReferencePhase::kReleasedPendingCapture;
    status_="Resolving Chrome page identity…";
    return;
  }
  AcceptReleased();
}

void MarkController::AcceptReleased() {
  if(!current_||!current_->released)return;
  auto job=store_.AcceptPending(current_->reference,current_->key_down_monotonic_ms,
                                current_->key_up_monotonic_ms);
  ++copy_generation_;current_->copy_generation=copy_generation_;
  last_copy_id_=current_->reference.id;
  if(job.state==ReferenceJobState::kPending&&pending_clipboard_) {
    try{current_->pasteboard_change_count=pending_clipboard_(current_->reference.id);}
    catch(...){current_->pasteboard_change_count.reset();}
    copy_result_=current_->pasteboard_change_count?CopyResult::kCopied:CopyResult::kFailed;
  }
  const auto accepted_generation=current_->generation;
  accepted_.push_back(std::move(current_));
  phase_=job.state!=ReferenceJobState::kPending?ReferencePhase::kAborted:
         ReferencePhase::kReleasedPendingCapture;
  status_=job.state!=ReferenceJobState::kPending?
      "Reference failed: "+(job.code.empty()?"identity_exists":job.code):
      "Reference loading…";
  if(job.state!=ReferenceJobState::kPending) {
    accepted_.pop_back();
    copy_result_=CopyResult::kFailed;
    return;
  }
  SubmitIfReady(accepted_generation);
}

void MarkController::SubmitIfReady(std::uint64_t generation) {
  auto found=std::find_if(accepted_.begin(),accepted_.end(),[&](const auto& item){
    return item->generation==generation;});
  if(found==accepted_.end()||(*found)->submitted)return;
  auto& transaction=**found;
  auto weak=std::weak_ptr<int>(lifetime_);
  auto dispatcher=dispatcher_;
  auto completion=[this,weak,dispatcher=std::move(dispatcher)](
      ReferenceJobSnapshot job) mutable {
    dispatcher([this,weak,job=std::move(job)]() mutable {
      if(!weak.lock())return;Completed(std::move(job));
    });
  };
  if(!transaction.capture_error.empty()) {
    transaction.submitted=true;
    store_.Fail(transaction.reference.id,transaction.capture_error,
                std::move(completion));return;
  }
  if(!transaction.capture)return;
  transaction.submitted=true;
  store_.FinalizeAsync(transaction.reference.id,std::move(*transaction.capture),
                       std::move(completion));
}

void MarkController::Completed(ReferenceJobSnapshot job) {
  auto transaction=std::find_if(accepted_.begin(),accepted_.end(),[&](const auto& item){
    return item->reference.id==job.id;});
  const bool owns_ui=transaction!=accepted_.end()&&
      (*transaction)->copy_generation==copy_generation_;
  if(pending_clipboard_) {
    if(owns_ui&&job.state==ReferenceJobState::kReady) {
      // The URL publication happened at release. Completion only reports the
      // reference lifecycle; it cannot read or establish clipboard ownership.
      if(!(*transaction)->pasteboard_change_count)
        copy_result_=CopyResult::kFailed;
      else if(ready_clipboard_)try {
        // This callback is an optional ownership guard supplied by the caller;
        // a successful result does not turn completion into a publication.
        if(!ready_clipboard_(job.ready,
                             *(*transaction)->pasteboard_change_count))
          copy_result_=CopyResult::kPersistedClipboardFailed;
      } catch(...) {copy_result_=CopyResult::kPersistedClipboardFailed;}
      status_="Reference ready; click its mark to copy the URL";
      if(!current_)phase_=ReferencePhase::kCommitted;
    } else if(owns_ui) {
      copy_result_=CopyResult::kFailed;status_="Reference failed: "+job.code;
      if(!current_)phase_=ReferencePhase::kAborted;
    }
  } else if(job.state==ReferenceJobState::kReady&&job.ready) {
    bool copied=false;
    if(clipboard_)try{copied=clipboard_(*job.ready);}catch(...){copied=false;}
    copy_result_=copied?CopyResult::kCopied:CopyResult::kPersistedClipboardFailed;
    status_=copied?"Reference ready and copied":
        "Reference ready; click its mark to retry";
    if(!current_)phase_=ReferencePhase::kCommitted;
  } else if(!pending_clipboard_) {
    copy_result_=CopyResult::kFailed;status_="Reference failed: "+job.code;
    if(!current_)phase_=ReferencePhase::kAborted;
  }
  if(transaction!=accepted_.end())accepted_.erase(transaction);
}

void MarkController::Abort(std::string reason) {
  if(!current_)return;++generation_;current_.reset();phase_=ReferencePhase::kAborted;
  copy_result_=CopyResult::kFailed;status_="Reference cancelled: "+std::move(reason);
}

void MarkController::Expire(Stamp now) {
  if(current_&&now.monotonic_us-current_->began.monotonic_us>30'000'000)
    Abort("drawing transaction timed out");
}

bool MarkController::AllowsDrawing() const {
  return current_&&current_->reference.context.pid>0&&!current_->released;
}

void MarkController::SetWindowObservation(WindowObservation observation) {
  if(observation.generation<=window_observation_.generation)return;
  window_observation_=std::move(observation);
}

void MarkController::SetPageObservation(PageObservation observation) {
  if (observation.generation <= page_observation_.generation) return;
  if (observation.availability == PageAvailability::kReady &&
      (!observation.identity || !ValidPageIdentity(*observation.identity))) {
    observation.availability=PageAvailability::kUnavailable;
    observation.identity.reset();
  }
  page_observation_=std::move(observation);
}

std::vector<ReferenceMark> MarkController::marks() const {
  std::vector<ReferenceMark> result;
  for(const auto& job:store_.Jobs())if(job.state==ReferenceJobState::kReady) {
    if(!VisibleJob(job,window_observation_,page_observation_))continue;
    if(job.ready)result.push_back(LightweightMark(job.ready->value));
    else {
      ReferenceMark mark;mark.value.id=job.id;mark.value.mark_id=job.id;
      mark.value.context=job.context;mark.value.path=job.path;
      mark.value.regions=job.regions;
      mark.value.circle_completed=job.circle_completed;
      mark.value.crop_margin=job.crop_margin;result.push_back(std::move(mark));
    }
  }
  return result;
}

std::vector<ReferenceJobSnapshot> MarkController::jobs() const {
  auto result=store_.Jobs();result.erase(std::remove_if(result.begin(),result.end(),
    [](const auto& job){return job.state==ReferenceJobState::kExpired||
      job.state==ReferenceJobState::kDeleted||
      job.state==ReferenceJobState::kReady;}),result.end());
  result.erase(std::remove_if(result.begin(),result.end(),[&](const auto& job) {
    return !VisibleJob(job,window_observation_,page_observation_);
  }),result.end());
  return result;
}

std::vector<ReferenceJobSnapshot> MarkController::inspector_jobs() const {
  auto result=store_.MetadataJobs();
  result.erase(std::remove_if(result.begin(),result.end(),[](const auto& job) {
    return job.state==ReferenceJobState::kDeleted;
  }),result.end());
  return result;
}

bool MarkController::NeedsChromePageObservation() const {
  if(current_&&current_->reference.context.bundle_id=="com.google.Chrome")
    return true;
  for(const auto& job:store_.MetadataJobs())
    if(job.context.bundle_id=="com.google.Chrome"&&
       job.state!=ReferenceJobState::kDeleted&&
       job.state!=ReferenceJobState::kExpired)return true;
  return false;
}

bool MarkController::AwaitingPageIdentity(std::uint64_t generation) const {
  return Current(generation)&&current_->released&&
      current_->reference.context.bundle_id=="com.google.Chrome"&&
      !current_->reference.page_identity;
}

bool MarkController::Recopy(std::size_t index) {
  const auto retained=marks();if(index>=retained.size())return false;
  return RecopyJob(retained[index].value.id);
}

bool MarkController::RecopyJob(std::string_view id) {
  auto job=store_.LookupMetadata(id);if(!job)return false;++copy_generation_;
  if(!VisibleJob(*job,window_observation_,page_observation_)) {
    copy_result_=CopyResult::kFailed;
    status_="Reference hidden for current window";
    return false;
  }
  last_copy_id_=std::string(id);
  if(job->state==ReferenceJobState::kExpired) {
    copy_result_=CopyResult::kFailed;status_="Reference expired";return false;
  }
  if(job->state==ReferenceJobState::kDeleted) {
    copy_result_=CopyResult::kFailed;status_="Reference deleted";return false;
  }
  if(pending_clipboard_) {
    auto receipt=pending_clipboard_(id);for(auto& accepted:accepted_)
      if(accepted->reference.id==id){accepted->copy_generation=copy_generation_;
        accepted->pasteboard_change_count=receipt;}
    copy_result_=receipt?CopyResult::kCopied:CopyResult::kFailed;
    status_=receipt?"Reference URL copied":"Reference URL could not be copied";
    return receipt.has_value();
  }
  if(job->state!=ReferenceJobState::kReady)return false;
  auto ready=store_.Lookup(id);if(!ready||!ready->ready)return false;
  bool success=false;try{success=clipboard_&&clipboard_(*ready->ready);}
  catch(...){success=false;}
  copy_result_=success?CopyResult::kCopied:CopyResult::kPersistedClipboardFailed;
  status_=success?"Reference URL copied":
      "Reference saved; clipboard failed. Click its mark to retry.";
  return success;
}

bool MarkController::CopyHistoryJob(std::string_view id) {
  const auto metadata=store_.LookupMetadata(id);
  if(!metadata || metadata->state!=ReferenceJobState::kReady) {
    copy_result_=CopyResult::kFailed;
    status_="Reference is not ready for history copy";
    return false;
  }
  const auto ready=store_.Lookup(id);
  if(!ready || ready->state!=ReferenceJobState::kReady || !ready->ready) {
    copy_result_=CopyResult::kFailed;
    status_="Persisted reference is unavailable for history copy";
    return false;
  }
  ++copy_generation_;
  last_copy_id_=std::string(id);
  bool copied=false;
  if(pending_clipboard_) {
    try {copied=pending_clipboard_(id).has_value();}
    catch(...) {copied=false;}
  } else if(clipboard_) {
    try {copied=clipboard_(*ready->ready);}
    catch(...) {copied=false;}
  }
  copy_result_=copied?CopyResult::kCopied:CopyResult::kPersistedClipboardFailed;
  status_=copied?"Reference URL copied":"Reference URL could not be copied";
  return copied;
}

DeleteResult MarkController::DeleteJob(std::string_view id) {
  ++copy_generation_;
  auto metadata=store_.LookupMetadata(id);
  if(!metadata ||
     (metadata->state!=ReferenceJobState::kDeleted&&
      !VisibleJob(*metadata,window_observation_,page_observation_))) {
    status_="Reference hidden for current window";
    return DeleteResult::kNotFound;
  }
  return DeleteStoredJob(id);
}

DeleteResult MarkController::DeleteHistoryJob(std::string_view id) {
  ++copy_generation_;
  if(!store_.LookupMetadata(id)) {
    status_="Reference not found";
    return DeleteResult::kNotFound;
  }
  return DeleteStoredJob(id);
}

DeleteResult MarkController::DeleteStoredJob(std::string_view id) {
  const auto result=store_.Delete(id);
  accepted_.erase(std::remove_if(accepted_.begin(),accepted_.end(),
    [&](const auto& transaction){return transaction->reference.id==id;}),
    accepted_.end());
  if(result==DeleteResult::kDeleted)status_="Reference deleted from this Mac";
  else if(result==DeleteResult::kAlreadyDeleted)status_="Reference already deleted";
  else if(result==DeleteResult::kCleanupFailed)
    status_="Delete incomplete: local reference cleanup failed";
  else status_="Reference not found";
  return result;
}

std::optional<std::size_t> MarkController::Hit(DisplayPoint point,double radius,
    const std::vector<FrozenDisplay>& displays) const {
  if(!point.IsValid()||point.unit!=CoordinateUnit::kLogicalPoints||
     !std::isfinite(radius)||radius<0)return {};
  const auto retained=marks();
  for(std::size_t index=retained.size();index>0;--index)
    if(HitRegions(retained[index-1].value,point,radius,displays))return index-1;
  return {};
}

std::optional<std::string> MarkController::HitJob(
    DisplayPoint point,double radius,
    const std::vector<FrozenDisplay>& displays) const {
  if(!point.IsValid()||point.unit!=CoordinateUnit::kLogicalPoints||
     !std::isfinite(radius)||radius<0)return {};
  auto snapshots=jobs();
  for(auto job=snapshots.rbegin();job!=snapshots.rend();++job)
    {
      Reference value;value.context=job->context;value.path=job->path;
      value.regions=job->regions;
      value.page_identity=job->page_identity;
      if(HitRegions(value,point,radius,displays))return job->id;
    }
  return {};
}

std::optional<std::string> MarkController::HitIdentity(
    DisplayPoint point,double radius,
    const std::vector<FrozenDisplay>& displays) const {
  if(!point.IsValid()||point.unit!=CoordinateUnit::kLogicalPoints||
     !std::isfinite(radius)||radius<0)return {};
  auto snapshots=store_.Jobs();
  for(auto job=snapshots.rbegin();job!=snapshots.rend();++job) {
    if(job->state==ReferenceJobState::kExpired||
       job->state==ReferenceJobState::kDeleted)continue;
    if(!VisibleJob(*job,window_observation_,page_observation_))continue;
    Reference value;value.context=job->context;value.path=job->path;
    value.regions=job->regions;
    value.page_identity=job->page_identity;
    if(job->metadata)value=*job->metadata;
    else if(job->ready)value=job->ready->value;
    if(HitRegions(value,point,radius,displays))return job->id;
  }
  return {};
}
}  // namespace seethis::core
