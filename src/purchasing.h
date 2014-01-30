#ifndef _PURCHASING_H__
#define _PURCHASING_H__

#include <string>
#include <vector>

class ProductInfo
{
public:
    std::string id;
    int priceInCents;
    std::string name;
};

void buyProduct(const std::string& id);
ProductInfo getProductInfo(const std::string& id);
std::vector<std::string> getAllProducts();
int doesOwnProduct(const std::string& id);

bool showPurchasingSplash();
void showOuyaControls();

#endif
