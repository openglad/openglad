#include "purchasing.h"
#include "SDL.h"
#include "util.h"
#include <jni.h>

// Cache for ownership
bool owns_full_game = false;

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
    mEnv->CallStaticVoidMethod(mActivityClass, midBuyProduct, jid);
    mEnv->DeleteLocalRef(jid);
    
    jint price = mEnv->CallStaticIntMethod(mActivityClass, midGetProductPrice, jid);
    
    jstring name_obj = (jstring) mEnv->CallStaticObjectMethod(mActivityClass, midGetProductName, jid);
    const char* name = mEnv->GetStringUTFChars(name_obj, NULL);
    
    ProductInfo result;
    result.id = id;
    result.priceInCents = price;
    result.name = name;
    
    mEnv->ReleaseStringUTFChars(name_obj, name);
    
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

bool doesOwnFullGame()
{
    // Check current cache
    if(owns_full_game)
        return true;
    
    // Check file cache (prone to piracy)
    SDL_RWops* infile = open_read_file("cfg/purchase.dat");
    if(infile != NULL)
    {
        SDL_RWclose(infile);
        owns_full_game = true;
        return true;
    }
    
    // Update cache from network
    int result = doesOwnProduct(FULL_GAME_PRODUCT_ID);
    
    if(result == 1)
        owns_full_game = true;
    else if(result == 0)
        owns_full_game = false;
    
    if(owns_full_game)
    {
        SDL_RWops* outfile = open_write_file("cfg/purchase.dat");
        SDL_RWwrite(outfile, "1", 1, 1);
        SDL_RWclose(outfile);
    }
    return owns_full_game;
}

void test_purchasing()
{
    bool result = doesOwnFullGame();
    if(result)
        Log("Already own game.\n");
    else
        Log("Does not own game.\n");
    
    buyProduct(FULL_GAME_PRODUCT_ID);
    
    result = doesOwnFullGame();
    if(result)
        Log("Yep, bought game.\n");
    else
        Log("Nope, didn't buy game.\n");
        
    Log("all_products:\n");
    std::vector<std::string> all_products = getAllProducts();
    for(std::vector<std::string>::iterator e = all_products.begin(); e != all_products.end(); e++)
    {
        ProductInfo p = getProductInfo(e->c_str());
        Log("%s (%s): %d cents\n", p.name.c_str(), p.id.c_str(), p.priceInCents);
    }
}
