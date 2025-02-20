#pragma once
#include "Order.h"

class OrderModify {
    // Used to modify an order, either to change its price or quantity
public:
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity, int contract, int traderID)
        : orderId_{ orderId }
        , price_{ price }
        , side_{ side }
        , quantity_{ quantity }
        , contract_{ contract }
        , traderID_{ traderID }
    { }

    OrderId GetOrderId() const { return orderId_; }
    Price GetPrice() const { return price_; }
    Side GetSide() const { return side_; }
    Quantity GetQuantity() const { return quantity_; }
    int GetContract() const { return contract_; }
    int GetTraderID() const { return traderID_; }

    OrderPointer ToOrderPointer(OrderType type) const
    {
        return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity(), GetContract(), GetTraderID());
    }

private:
    OrderId orderId_;
    Price price_;
    Side side_;
    Quantity quantity_;
    int contract_;
    int traderID_;
};
