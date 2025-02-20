#pragma once

#include <list>
#include <exception>
#include <format>
#include <string>

#include "OrderType.h"
#include "Side.h"
#include "Usings.h"
#include "Constants.h"


class Order {
    public:
        Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity, int contract, int traderID)
            : orderType_{ orderType }
            , orderId_{ orderId }
            , side_{ side }
            , price_{ price }
            , initialQuantity_{ quantity }
            , remainingQuantity_{ quantity }
            , contract_{ contract }
            , traderID_{ traderID }
        { }

        Order(OrderId orderId, Side side, Quantity quantity)
            : Order(OrderType::Market, orderId, side, Constants::InvalidPrice, quantity, 1, -1)
        { }

        // Standard getters for order attributes
        OrderId GetOrderId() const { return orderId_; }
        Side GetSide() const { return side_; }
        Price GetPrice() const { return price_; }
        int GetContract() const { return contract_; }
        int GetTraderID() const { return traderID_; }
        OrderType GetOrderType() const { return orderType_; }
        Quantity GetInitialQuantity() const { return initialQuantity_; }
        Quantity GetRemainingQuantity() const { return remainingQuantity_; }
        Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
        
        bool IsFilled() const { return GetRemainingQuantity() == 0; }

        void Fill(Quantity quantity) {
            // Check that the order can be filled for the given quantity
            if (quantity > GetRemainingQuantity()) {
                std::cout << "Order cannot be filled for more than its remaining quantity" << GetOrderId() << std::endl;
                throw std::logic_error("Order cannot be filled for more than its remaining quantity");
                return;
            }

            remainingQuantity_ -= quantity;
        }

        void ToGoodTillCancel(Price price) { 
            if (GetOrderType() != OrderType::Market) {
                throw std::logic_error("Order (" + std::to_string(GetOrderId()) + ") cannot have its price adjusted, only market orders can.");
            }

            price_ = price;
            orderType_ = OrderType::GoodTillCancel;
        }

    private:
    // Private data members for every order
        OrderType orderType_;
        OrderId orderId_;
        Side side_;
        Price price_;
        Quantity initialQuantity_;
        Quantity remainingQuantity_;
        int contract_;
        int traderID_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;
