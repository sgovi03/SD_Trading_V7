
/*
PATCH: Early failure exit logic inside trade_manager.cpp
*/

if (trade.bars_in_trade > 12) {

    double move = std::abs(current_price - trade.entry_price);

    if (move < atr * 0.4) {

        LOG_INFO("Early failure exit triggered");
        exit_trade(symbol, direction, quantity, current_price);
        return;
    }
}
