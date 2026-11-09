Sint32 viewscreen::floor_glide_frames_left() const { return glide_frames_left_; }

float viewscreen::floor_glide_camera_z() const
{
    return glide_frames_left_ > 0 ? glide_camera_z_
                                 : static_cast<float>(current_floor_);
}

Sint32 viewscreen::floor_glide_cause() const { return static_cast<Sint32>(glide_cause_); }
