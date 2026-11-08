// One local filter for all Teacher windows. Buffered modifiers are bounded.
class TeacherKeyFilter {
public:
    struct Result { std::vector<MSG> forward; bool toggle=false; };
    void reset(){deferred_.clear();suppressed_=0;shortcut_=false;context_=0;}
    void focusChanged(){deferred_.clear();context_=0;}
    Result process(MSG message,bool control,bool shift,bool alt,uintptr_t context) {
        Result result;
        bool down=message.message==WM_KEYDOWN || message.message==WM_SYSKEYDOWN;
        bool up=message.message==WM_KEYUP || message.message==WM_SYSKEYUP;
        if(!down && !up) {result.forward.push_back(message);return result;}
        if(context_!=context) {deferred_.clear();context_=context;}
        unsigned bit=(message.wParam==VK_CONTROL || message.wParam==VK_LCONTROL || message.wParam==VK_RCONTROL)?1u:
            (message.wParam==VK_SHIFT || message.wParam==VK_LSHIFT || message.wParam==VK_RSHIFT)?2u:0u;
        bool repeat=(message.lParam&(LPARAM(1)<<30))!=0;
        if(down && bit) {
            if(!repeat) {suppressed_&=~bit;if(deferred_.size()<4)deferred_.push_back(message);}
            return result;
        }
        if(up && bit && (suppressed_&bit)) {suppressed_&=~bit;return result;}
        if(down && message.wParam==VK_F12 && control && shift && !alt) {
            deferred_.clear();suppressed_=3;
            result.toggle=!repeat;shortcut_=true;return result;
        }
        if(up && message.wParam==VK_F12 && shortcut_) {shortcut_=false;return result;}
        result.forward.swap(deferred_);result.forward.push_back(message);return result;
    }
private:
    std::vector<MSG> deferred_;unsigned suppressed_=0;bool shortcut_=false;uintptr_t context_=0;
};
