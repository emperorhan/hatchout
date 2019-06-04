/*
 * Copyright 2019 DE-labtory
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ghost.hpp"
using namespace eosio;

ACTION ghost::init( const public_key &key ) {
    require_auth( _self );
    check( !_info_state.pub_key.data.at(0), "public key is already registered" );
    _info_state.pub_key = key;
}

ACTION ghost::issue( name to, asset quantity, string memo ) {
    require_auth( get_self() );

    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must issue positive quantity" );
    check( quantity.symbol == _info_state.supply.symbol, "symbol precision mismatch" );

    _info_state.supply += quantity;

    add_balance( get_self(), quantity, get_self() );

    if( to != get_self() ) {
        SEND_INLINE_ACTION( *this, transfer, { {get_self(), "active"_n} },
                            { get_self(), to, quantity, memo }
        );
    }
}

ACTION ghost::burn( name owner, asset quantity, string memo ) {
    require_auth( owner );

    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );

    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must burn positive quantity" );
    check( quantity.symbol == _info_state.supply.symbol, "symbol precision mismatch" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    sub_balance( owner, quantity );
    _info_state.supply -= quantity;

    allowances allws( get_self(), owner.value );
    auto existing_allw = allws.find( sym.code().raw() );
    if( existing_allw != allws.end() ) {
        accounts acnts( get_self(), owner.value );
        auto existing_ac = acnts.find( sym.code().raw() );
        if( !existing_ac->balance.amount ) allws.erase(existing_allw);
        else if( existing_allw->balance.amount > existing_ac->balance.amount ){
            allws.modify( existing_allw, same_payer, [&]( auto& a ) {
                a.balance.amount = existing_ac->balance.amount;
            });
        }
    }
}

ACTION ghost::burnfrom( name burner, name owner, asset quantity, string memo ) {
    require_auth( burner );
    check( burner != owner, "cannot burnfrom self" );
    check( is_account( owner ), "owner account does not exist");

    require_recipient( owner );

    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must be positive quantity" );
    check( quantity.symbol == _info_state.supply.symbol, "symbol precision mismatch" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    allowances allws( get_self(), owner.value );
    auto existing_allw = allws.require_find( sym.code().raw(), "no symbol in the allowance table" );
    check( existing_allw->spender == burner, "you are not a spender" );
    check( existing_allw->balance.amount >= quantity.amount, "burner does not have enough allowed amount" );
    
    if( existing_allw->balance.amount == quantity.amount ) allws.erase( existing_allw );
    else {
        allws.modify( existing_allw, same_payer, [&]( auto& a ) {
            a.balance -= quantity;
        });
    }

    sub_balance( owner, quantity );

    _info_state.supply -= quantity;
}

ACTION ghost::transfer( name         from,
                        name         to,
                        asset        quantity,
                        string       memo ) {
    check( from != to, "cannot transfer to self" );
    require_auth( from );
    check( is_account( to ), "to account does not exist");

    require_recipient( from );
    require_recipient( to );

    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must transfer positive quantity" );
    check( quantity.symbol == _info_state.supply.symbol, "symbol precision mismatch" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    auto payer = has_auth( to ) ? to : from;

    sub_balance( from, quantity );
    add_balance( to, quantity, payer );

    allowances allws( get_self(), from.value );
    auto existing_allw = allws.find( sym.code().raw() );
    if( existing_allw != allws.end() ) {
        accounts acnts( get_self(), from.value );
        auto existing_ac = acnts.find( sym.code().raw() );
        if( !existing_ac->balance.amount ) allws.erase(existing_allw);
        else if( existing_allw->balance.amount > existing_ac->balance.amount ){
            allws.modify( existing_allw, same_payer, [&]( auto& a ) {
                a.balance.amount = existing_ac->balance.amount;
            });
        }
    }
}

ACTION ghost::approve( name owner, name spender, asset quantity ) {
    require_auth( owner );

    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must issue positive quantity" );
    check( quantity.symbol == _info_state.supply.symbol, "symbol precision mismatch" );

    accounts acnts( get_self(), owner.value );
    auto existing_ac = acnts.require_find( sym.code().raw(), "owner does not have token with symbol" );

    check( existing_ac->balance.amount >= quantity.amount, "not enough balance" );
    
    allowances allws( get_self(), owner.value );
    auto existing_allw = allws.find( sym.code().raw() );
    if( existing_allw == allws.end() ) {
        allws.emplace( owner, [&]( auto& a ){
            a.balance = quantity;
            a.spender = spender;
        });
    } else {
        allws.modify( existing_allw, same_payer, [&]( auto& a ){
            a.balance = quantity;
            a.spender = spender;
        });
    }
}

ACTION ghost::transferfrom( name spender, name from, name to, asset quantity, string memo ) {
    require_auth( spender );
    check( from != to, "cannot transfer to self" );
    check( is_account( from ), "from account does not exist");
    check( is_account( to ), "to account does not exist");
    check( spender != from, "spender and from must be different" );

    require_recipient( from );
    require_recipient( to );

    auto sym = quantity.symbol;
    check( sym.is_valid(), "invalid symbol name" );
    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must transfer positive quantity" );
    check( quantity.symbol == _info_state.supply.symbol, "symbol precision mismatch" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    allowances allws( get_self(), from.value );
    auto existing_allw = allws.require_find( sym.code().raw(), "no symbol in the allowance table" );
    check( existing_allw->balance.amount >= quantity.amount, "spender does not have enough allowed amount" );

    if( existing_allw->balance.amount == quantity.amount ) allws.erase( existing_allw );
    else {
        allws.modify( existing_allw, same_payer, [&]( auto& a ) {
            a.balance -= quantity;
        });
    }

    auto payer = has_auth( to ) ? to : spender;

    sub_balance( from, quantity );
    add_balance( to, quantity, payer );
}

ACTION ghost::incallowance( name owner, asset quantity ) {
    require_auth( owner );

    auto sym = quantity.symbol;
    
    accounts acnts( get_self(), owner.value );
    auto existing_ac = acnts.require_find( sym.code().raw(), "owner does not have token with symbol" );

    allowances allws( get_self(), owner.value );
    auto existing_allw = allws.require_find( sym.code().raw(), "spender is not registed" );

    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must issue positive quantity" );
    check( quantity.symbol == _info_state.supply.symbol, "symbol precision mismatch" );
    check( existing_ac->balance.amount >= existing_allw->balance.amount + quantity.amount, "owner does not have enough increase allow amount" );

    allws.modify( existing_allw, same_payer, [&]( auto& a ) {
        a.balance += quantity;
    });
}

ACTION ghost::decallowance( name owner, asset quantity ) {
    require_auth( owner );

    auto sym = quantity.symbol;

    allowances allws( get_self(), owner.value );
    auto existing_allw = allws.require_find( sym.code().raw(), "spender is not registed" );

    check( quantity.is_valid(), "invalid quantity" );
    check( quantity.amount > 0, "must issue positive quantity" );
    check( quantity.symbol == _info_state.supply.symbol, "symbol precision mismatch" );
    check( existing_allw->balance >= quantity, "there is not enough balance" );

    allws.modify( existing_allw, same_payer, [&]( auto& a ) {
        a.balance -= quantity;
    });
}

ACTION ghost::open( name owner, const symbol& symbol, name ram_payer ) {
    require_auth( ram_payer );

    auto sym_code_raw = symbol.code().raw();

    check( _info_state.supply.symbol == symbol, "symbol precision mismatch" );

    accounts acnts( get_self(), owner.value );
    auto existing_ac = acnts.find( sym_code_raw );
    if( existing_ac == acnts.end() ) {
        acnts.emplace( ram_payer, [&]( auto& a ){
            a.balance = asset{0, symbol};
        });
    }
}

ACTION ghost::close( name owner, const symbol& symbol ) {
    require_auth( owner );
    accounts acnts( get_self(), owner.value );
    auto existing_ac = acnts.find( symbol.code().raw() );
    check( existing_ac != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
    check( existing_ac->balance.amount == 0, "Cannot close because the balance is not zero." );
    acnts.erase( existing_ac );
}

ACTION ghost::issuenft( name to, uint64_t token_id, name token_name, asset value, const signature &sig, string memo ) {
    require_auth(to);

    string data = to.to_string() + uint64_to_string(token_id) + token_name.to_string() + value.to_string();
    const checksum256 digest = sha256(&data[0], data.size());
    assert_recover_key( digest, sig, _info_state.pub_key );

    check( is_account(to), "to account does not exist" );

    auto sym = _info_state.ghost_supply.symbol;
    check( sym.is_valid(), "invalid symbol name" );

    asset unit( 1, sym );

    _info_state.ghost_supply += unit;

    tokens tokenstable( _self, sym.code().raw() );

    auto existing_tk = tokenstable.find( token_id );
    check( existing_tk == tokenstable.end(), "token with symbol already exists" );
    tokenstable.emplace( to, [&]( auto& token ) {
        token.token_id  = token_id;
        token.owner     = to;
        token.value     = value;
        token.tokenName = token_name;
        token.spender   = to;
    });

    add_balance( to, unit, to );
}

ACTION ghost::burnnft( name owner, vector<uint64_t> token_ids, string memo ) {
    require_auth( owner );

    auto sym = _info_state.ghost_supply.symbol;
    check( sym.is_valid(), "invalid symbol name" );

    check( memo.size() <= 256, "memo has more than 256 bytes" );

    asset unit( token_ids.size(), sym );
    check( unit.amount, "enter token id" );

    tokens tokenstable( _self, sym.code().raw() );
    for( auto const& token_id : token_ids ) {
        auto existing_tk = tokenstable.find( token_id );
        check( existing_tk != tokenstable.end(), "token with symbol does not exists" );
        check( existing_tk->owner == owner, "not the owner of token" );
        tokenstable.erase( existing_tk );
    }

    sub_balance( owner, unit );
    
    _info_state.ghost_supply -= unit;
}

ACTION ghost::burnnftfrom( name burner, id_type token_id, string memo ) {
    require_auth( burner );

    auto symbol = _info_state.ghost_supply.symbol;
    check( symbol.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    tokens tokenstable( _self, symbol.code().raw() );
    auto existing_tk = tokenstable.find( token_id );
    check( existing_tk != tokenstable.end(), "token with symbol does not exists" );

    require_recipient( existing_tk->owner );

    check( burner == existing_tk->spender, "burner is not token spender" );

    asset unit( 1, symbol );

    sub_balance( existing_tk->owner, unit );

    tokenstable.erase( existing_tk );
    _info_state.ghost_supply -= unit;
}

ACTION ghost::send( name from, name to, id_type token_id, string memo ) {
    check( from != to, "cannot transfer to self" );
    require_auth( from );
    check( is_account( to ), "to account does not exist" );

    require_recipient( from );
    require_recipient( to );

    auto symbol = _info_state.ghost_supply.symbol;
    check( symbol.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    tokens tokenstable( _self, symbol.code().raw() );
    auto existing_tk = tokenstable.find( token_id );
    check( existing_tk != tokenstable.end(), "token with symbol does not exists" );
    check( from == existing_tk->owner, "not the owner of token" );
    check( existing_tk->spender != _self, "if spender is _self, it can not transfer" );

    auto payer = has_auth( to ) ? to : from;
    
    tokenstable.modify( existing_tk, payer, [&]( auto& token ) {
        token.owner     = to;
        token.spender   = to;
    });

    asset unit( 1, symbol );

    sub_balance( from, unit );
    add_balance( to, unit, payer );
}

ACTION ghost::approvenft( name owner, name spender, id_type token_id ) {
    require_auth( owner );

    auto symbol = _info_state.ghost_supply.symbol;
    check( symbol.is_valid(), "invalid symbol name" );

    tokens tokenstable( _self, symbol.code().raw() );
    auto existing_tk = tokenstable.find( token_id );
    check( existing_tk != tokenstable.end(), "token with symbol does not exists" );
    check( owner == existing_tk->owner, "not the owner of token" );
    check( owner == _self || existing_tk->spender != _self, "if spender is _self, it can not be changed" );

    tokenstable.modify( existing_tk, same_payer, [&]( auto& token ) {
        token.spender = spender;
    });
}

ACTION ghost::sendfrom( name spender, name to, id_type token_id, string memo ) {
    require_auth( spender );

    check( is_account( to ), "to account does not exist");

    auto symbol = _info_state.ghost_supply.symbol;
    check( symbol.is_valid(), "invalid symbol name" );
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    tokens tokenstable( _self, symbol.code().raw() );
    auto existing_tk = tokenstable.find( token_id );
    check( existing_tk != tokenstable.end(), "token with symbol does not exists" );
    check( spender == existing_tk->spender, "spender is not token spender" );
    check( spender != existing_tk->owner, "spender and owner must be different" );
    name owner = existing_tk->owner;

    require_recipient( owner );
    require_recipient( to );
    
    auto payer = has_auth( to ) ? to : spender;

    tokenstable.modify( existing_tk, payer, [&]( auto& token ) {
        token.owner     = to;
        token.spender   = to;
    });

    asset unit( 1, symbol );

    sub_balance( owner, unit );
    add_balance( to, unit, payer );
}

ACTION ghost::auctiontoken( name auctioneer, id_type token_id, asset min_price, uint32_t sec ) {
    require_auth( auctioneer );

    require_recipient( auctioneer );
    require_recipient( _self );

    auto symbol = _info_state.ghost_supply.symbol;
    check( symbol.is_valid(), "invalid symbol name" );

    check( sec > 0, "sec must be a positive integer" );

    const time_point_sec deadline = time_point_sec(now()) + sec;

    tokens tokenstable( _self, symbol.code().raw() );
    auto existing_tk = tokenstable.find( token_id );
    check( existing_tk != tokenstable.end(), "token with symbol does not exists" );
    check( existing_tk->owner == auctioneer, "not the owner of token" );

    token_bids tokenbidstable( _self, symbol.code().raw() );
    auto existing_bid = tokenbidstable.find( token_id );
    check( existing_bid == tokenbidstable.end(), "token bid already exist" );

    check( min_price.amount > 0, "token bid already exist" );

    tokenbidstable.emplace( auctioneer, [&]( auto& b ){
        b.token_id    = token_id;
        b.high_bidder = auctioneer;
        b.high_bid    = min_price.amount;
        b.deadline    = deadline;
    });
    
    tokenstable.modify( existing_tk, same_payer, [&]( auto& token ) {
        token.spender = _self;
    });
}

ACTION ghost::bidtoken( name bidder, id_type token_id, asset bid ) {
    require_auth( bidder );

    check( bid.symbol == _info_state.supply.symbol, "bid asset must be key currency symbol" );

    auto symbol = _info_state.ghost_supply.symbol;
    check( symbol.is_valid(), "invalid symbol name" );

    tokens tokenstable( _self, symbol.code().raw() );
    auto existing_tk = tokenstable.find( token_id );
    check( existing_tk != tokenstable.end(), "token with symbol does not exists" );

    check( bidder != existing_tk->owner, "token owners can not bid" );

    token_bids tokenbidstable( _self, symbol.code().raw() );
    auto existing_bid = tokenbidstable.find( token_id );
    check( existing_bid != tokenbidstable.end(), "token auction is not exist" );

    const time_point_sec time_now = time_point_sec(now());
    check( existing_bid->deadline > time_now, "the auction deadline has passed" );
    check( bid.amount > existing_bid->high_bid, "the bid amount is insufficient" );

    if( existing_bid->high_bidder != existing_tk->owner ) {
        asset refund_kc(existing_bid->high_bid, _info_state.supply.symbol);
        // refund
        action(
            permission_level{ _self, "active"_n },
            _self, "transfer"_n,
            std::make_tuple( _self, existing_bid->high_bidder, refund_kc, std::string("refund bidding fee"))
        ).send();
    }

    // new high bidder
    tokenbidstable.modify( existing_bid, same_payer, [&]( auto& b ){
        b.high_bidder = bidder;
        b.high_bid    = bid.amount; 
    });
    
    sub_balance( bidder, bid );
    add_balance( _self, bid, _self );
    
    bidresult_action bid_act( bidder, std::vector<eosio::permission_level>{ } );
    bid_act.send( bid );
}

ACTION ghost::claimtoken( name requester, id_type token_id ) {
    require_auth(requester);

    auto symbol = _info_state.ghost_supply.symbol;
    check( symbol.is_valid(), "invalid symbol name" );

    tokens tokenstable( _self, symbol.code().raw() );
    auto existing_tk = tokenstable.find( token_id );
    check( existing_tk != tokenstable.end(), "token with symbol does not exists" );

    token_bids tokenbidstable( _self, symbol.code().raw() );
    auto existing_bid = tokenbidstable.find( token_id );
    check( existing_bid != tokenbidstable.end(), "token auction is not exist" );

    const time_point_sec time_now = time_point_sec(now());
    check( existing_bid->deadline <= time_now, "deadline not over" );
    check( requester == existing_tk->owner || requester == existing_bid->high_bidder, "the requester is not authorized" );

    if( existing_bid->high_bidder != existing_tk->owner ) {
        asset payment_kc(existing_bid->high_bid, _info_state.supply.symbol);

        // bidding fee payment
        action(
            permission_level{ _self, "active"_n },
            _self, "transfer"_n,
            std::make_tuple( _self, existing_tk->owner, payment_kc, std::string("receive auction sale money"))
        ).send();

        // nft ownership change
        action(
            permission_level{ _self, "active"_n },
            _self, "sendfrom"_n,
            std::make_tuple( _self, existing_bid->high_bidder, token_id, std::string("receive bid tokens"))
        ).send();
    } else {
        tokenstable.modify( existing_tk, same_payer, [&]( auto& token ) {
            token.spender = existing_tk->owner;
        });
    }

    tokenbidstable.erase( existing_bid );
}

void ghost::sub_balance( name owner, asset value ) {
    accounts from_acnts( _self, owner.value );

    const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
    check( from.balance.amount >= value.amount, "overdrawn balance" );

    name payer = !has_auth(owner) ? same_payer : owner;

    from_acnts.modify( from, payer, [&]( auto& a ) {
        a.balance -= value;
    });
}

void ghost::add_balance( name owner, asset value, name ram_payer ) {
    accounts to_acnts( _self, owner.value );
    auto to = to_acnts.find( value.symbol.code().raw() );
    if( to == to_acnts.end() ) {
        to_acnts.emplace( ram_payer, [&]( auto& a ){
            a.balance = value;
        });
    } else {
        to_acnts.modify( to, same_payer, [&]( auto& a ) {
            a.balance += value;
        });
    }
}