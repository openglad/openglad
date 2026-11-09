namespace og::curses {

const SaveData& CursesPickerClient::save_data() const { return save_data_; }

SaveData& CursesPickerClient::save_data() { return save_data_; }

int CursesPickerClient::difficulty() const { return options_.difficulty; }

} // namespace og::curses
