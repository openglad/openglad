#include "purchasing.h"
#include "SDL.h"
#include "util.h"
#include <jni.h>

static jclass mActivityClass;

static jmethodID midBuyProduct;
static jmethodID midGetProductPrice;
static jmethodID midGetProductName;
static jmethodID midDoesOwnProduct;
static jmethodID midGetAllProducts;

extern "C" void initMethods(JNIEnv* mEnv);

extern "C" void Java_com_dinomage_openglad_Openglad_initJNIConnection(JNIEnv* mEnv, jclass cls)
{
    mActivityClass = (jclass)(mEnv->NewGlobalRef(cls));
    
    initMethods(mEnv);
}

// This exists because it seemed like I couldn't get a valid reference to a returned array from Java.
// Now I'm passing it as an argument to guarantee persistence.
static std::vector<std::string> hack_string_array;

extern "C" void Java_com_dinomage_openglad_Openglad_passStringArrayHack(JNIEnv* mEnv, jclass cls, jobjectArray arr)
{
    hack_string_array.clear();
    jsize len = mEnv->GetArrayLength(arr);
    
    for(size_t i = 0; i < len; i++)
    {
        jstring elem = (jstring) mEnv->GetObjectArrayElement(arr, i);
        const char* id = mEnv->GetStringUTFChars(elem, NULL);
        
        if(id != NULL)
            hack_string_array.push_back(id);
        
        mEnv->ReleaseStringUTFChars(elem, id);
        mEnv->DeleteLocalRef(elem);
    }
}

extern "C" void initMethods(JNIEnv* mEnv)
{
    midBuyProduct = mEnv->GetStaticMethodID(mActivityClass, "buyProduct","(Ljava/lang/String;)V");
    midDoesOwnProduct = mEnv->GetStaticMethodID(mActivityClass, "doesOwnProduct","(Ljava/lang/String;)I");
    midGetProductPrice = mEnv->GetStaticMethodID(mActivityClass, "getProductPrice","(Ljava/lang/String;)I");
    midGetProductName = mEnv->GetStaticMethodID(mActivityClass, "getProductName","(Ljava/lang/String;)Ljava/lang/String;");
    midGetAllProducts = mEnv->GetStaticMethodID(mActivityClass, "getAllProducts","()V");
}

#define FULL_GAME_PRODUCT_ID "gladiator_full_game"
#define TIP_PRODUCT_ID "gladiator_tip"

void buyProduct(const std::string& id)
{
    JNIEnv *mEnv = (JNIEnv*)SDL_AndroidGetJNIEnv();
    
    jstring jid = mEnv->NewStringUTF(id.c_str());
    mEnv->CallStaticVoidMethod(mActivityClass, midBuyProduct, jid);
    mEnv->DeleteLocalRef(jid);
}

ProductInfo getProductInfo(const std::string& id)
{
    JNIEnv *mEnv = (JNIEnv*)SDL_AndroidGetJNIEnv();
    
    jstring jid = mEnv->NewStringUTF(id.c_str());
    
    jint price = mEnv->CallStaticIntMethod(mActivityClass, midGetProductPrice, jid);
    
    jstring name_obj = (jstring) mEnv->CallStaticObjectMethod(mActivityClass, midGetProductName, jid);
    const char* name = mEnv->GetStringUTFChars(name_obj, NULL);
    
    ProductInfo result;
    result.id = id;
    result.priceInCents = price;
    result.name = name;
    
    mEnv->ReleaseStringUTFChars(name_obj, name);
    mEnv->DeleteLocalRef(jid);
    
    return result;
}

std::vector<std::string> getAllProducts()
{
    JNIEnv *mEnv = (JNIEnv*)SDL_AndroidGetJNIEnv();
    
    mEnv->CallStaticObjectMethod(mActivityClass, midGetAllProducts);
    
    return hack_string_array;
}

int doesOwnProduct(const std::string& id)
{
    JNIEnv *mEnv = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jstring jid = mEnv->NewStringUTF(id.c_str());
    
    // 1: Yes, 0: No, -1: Wait..., -2: Network error
    jint result = mEnv->CallStaticIntMethod(mActivityClass, midDoesOwnProduct, jid);
    
    // Wait?
    while(result == -1)
    {
        SDL_Delay(1000);
        result = mEnv->CallStaticIntMethod(mActivityClass, midDoesOwnProduct, jid);
    }
    mEnv->DeleteLocalRef(jid);
    return result;
}



#include "sai2x.h"
#include "button.h"

#define OG_OK 4
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);

extern screen* myscreen;
extern Screen* E_Screen;

bool showPurchasingSplash()
{
    ProductInfo p = getProductInfo(TIP_PRODUCT_ID);
    if(p.priceInCents < 0)
    {
        popup_dialog("Error", "Network error...");
        return false;
    }
    
    SDL_RWops* rwops = open_read_file("pix/gladiator_demo_splash.bmp");
    if(rwops == NULL)
    {
        char buf[20];
        snprintf(buf, 20, "Tip $%d.%02d?", p.priceInCents/100, p.priceInCents%100);
        if(yes_or_no_prompt("Donate a tip?", buf, false))
            buyProduct(TIP_PRODUCT_ID);
        
        return true;
    }
    
    SDL_Surface* splash = SDL_LoadBMP_RW(rwops, 0);
    SDL_RWclose(rwops);
    
    if(splash == NULL)
    {
        char buf[20];
        snprintf(buf, 20, "Tip $%d.%02d?", p.priceInCents/100, p.priceInCents%100);
        if(yes_or_no_prompt("Donate a tip?", buf, false))
            buyProduct(TIP_PRODUCT_ID);
        
        return true;
    }
    
    text& loadtext = myscreen->text_normal;
    text& bigtext = myscreen->text_big;
    
    SDL_Rect no_button = {210, 170, 60, 10};
    SDL_Rect yes_button = {160, 170, 40, 20};
    
    // Controller input
    int retvalue = 0;
	int highlighted_button = 0;
	
	int no_index = 0;
	int yes_index = 1;
	
	button buttons[] = {
        button("THANKS!!", KEYSTATE_UNKNOWN, no_button.x, no_button.y, no_button.w, no_button.h, 0, -1 , MenuNav::Left(yes_index)),
        button("TIP", KEYSTATE_UNKNOWN, yes_button.x, yes_button.y, yes_button.w, yes_button.h, 0, -1 , MenuNav::Right(no_index))
	};
	
    char price_string[20];
    snprintf(price_string, 20, "$%d.%02d", p.priceInCents/100, p.priceInCents%100);
    
    
    bool done = false;
    while (!done)
    {
        // Reset the timer count to zero ...
        reset_timer();

        if (myscreen->end)
            break;

        // Get keys and stuff
        get_input_events(POLL);
		
        handle_menu_nav(buttons, highlighted_button, retvalue, false);

        // Mouse stuff ..
		MouseState& mymouse = query_mouse();
        int mx = mymouse.x;
        int my = mymouse.y;
        
        bool do_click = mymouse.left;
        bool do_yes = (do_click && yes_button.x <= mx && mx <= yes_button.x + yes_button.w
               && yes_button.y <= my && my <= yes_button.y + yes_button.h) || (retvalue == OG_OK && highlighted_button == yes_index);
        bool do_no = (do_click && no_button.x <= mx && mx <= no_button.x + no_button.w
               && no_button.y <= my && my <= no_button.y + no_button.h) || (retvalue == OG_OK && highlighted_button == no_index);
		if (mymouse.left)
		{
		    while(mymouse.left)
                get_input_events(WAIT);
		}

        // Choose
        if(do_yes)
        {
            buyProduct(TIP_PRODUCT_ID);
            done = true;
            break;
        }
        // Cancel
        else if(do_no)
        {
            done = true;
            break;
        }
       
        retvalue = 0;

        // Draw
        myscreen->clearbuffer();
        
        SDL_BlitSurface(splash, NULL, E_Screen->render, NULL);
        
        myscreen->draw_button(no_button.x, no_button.y, no_button.x + no_button.w, no_button.y + no_button.h, 1, 1);
        loadtext.write_xy(no_button.x + 2, no_button.y + 2, "THANKS!!", DARK_BLUE, 1);
        
        myscreen->draw_button(yes_button.x, yes_button.y, yes_button.x + yes_button.w, yes_button.y + yes_button.h, 1, 1);
        loadtext.write_xy(yes_button.x + 8, yes_button.y + 3, "TIP", DARK_BLUE, 1);
        loadtext.write_xy(yes_button.x + 2, yes_button.y + 2 + 10, price_string, DARK_BLUE, 1);

        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0, 0, 320, 200);
        SDL_Delay(10);
    }
    
    SDL_FreeSurface(splash);
    
    
    popup_dialog("You're Welcome!", "Enjoy Gladiator!");
    
    return true;
}


void showOuyaControls()
{
    SDL_RWops* rwops = open_read_file("pix/gladiator_ouya_controls.bmp");
    if(rwops == NULL)
        return;
    
    SDL_Surface* splash = SDL_LoadBMP_RW(rwops, 0);
    SDL_RWclose(rwops);
    
    if(splash == NULL)
        return;
    
    Uint32 starttime = SDL_GetTicks();
    bool waiting = true;
    
    clear_keyboard();
    
    bool done = false;
    while (!done)
    {
        // Reset the timer count to zero ...
        reset_timer();

        if (myscreen->end)
            break;

        // Get keys and stuff
        get_input_events(POLL);
        
        if(waiting && SDL_GetTicks() - starttime > 3000)
        {
            waiting = false;
            clear_keyboard();
        }
        else if(!waiting && query_input_continue())
            done = true;

        // Draw
        myscreen->clearbuffer();
        
        SDL_BlitSurface(splash, NULL, E_Screen->render, NULL);
        
        myscreen->buffer_to_screen(0, 0, 320, 200);
        SDL_Delay(10);
    }
    
    SDL_FreeSurface(splash);
}
