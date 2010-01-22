//
// Copyright 2010 Ettus Research LLC
//

#include <usrp_uhd/usrp/dboard/manager.hpp>
#include <boost/format.hpp>
#include <map>
#include "dboards.hpp"

using namespace usrp_uhd::usrp::dboard;

/***********************************************************************
 * register internal dboards
 *
 * Register internal/known dboards located in this build tree.
 * Each board should have entries below mapping an id to a constructor.
 * The xcvr type boards should register both rx and tx sides.
 *
 * This function will be called before new boards are registered.
 * This allows for internal boards to be externally overridden.
 * This function will also be called when creating a new manager
 * to ensure that the maps are filled with the entries below.
 **********************************************************************/
static void register_internal_dboards(void){
    //ensure that this function can only be called once per instance
    static bool called = false;
    if (called) return; called = true;
    //register the known dboards (dboard id, constructor, num subdevs)
    manager::register_subdevs(0x0000, &basic_tx::make, 1);
    manager::register_subdevs(0x0001, &basic_rx::make, 3);
}

/***********************************************************************
 * storage and registering for dboards
 **********************************************************************/
//map a dboard id to a dboard constructor
static std::map<manager::dboard_id_t, manager::dboard_ctor_t> id_to_ctor_map;

//map a dboard constructor to number of subdevices
static std::map<manager::dboard_ctor_t, size_t> ctor_to_num_map;

void manager::register_subdevs(
    dboard_id_t dboard_id,
    dboard_ctor_t dboard_ctor,
    size_t num_subdevs
){
    register_internal_dboards(); //always call first
    id_to_ctor_map[dboard_id] = dboard_ctor;
    ctor_to_num_map[dboard_ctor] = num_subdevs;
}

/***********************************************************************
 * internal helper classes
 **********************************************************************/
/*!
 * A special wax proxy object that forwards calls to a subdev.
 * A sptr to an instance will be used in the properties structure. 
 */
class subdev_proxy : boost::noncopyable, public wax::obj{
public:
    typedef boost::shared_ptr<subdev_proxy> sptr;
    enum type_t{RX_TYPE, TX_TYPE};

    //structors
    subdev_proxy(base::sptr subdev, type_t type)
    : _subdev(subdev), _type(type){
        /* NOP */
    }

    ~subdev_proxy(void){
        /* NOP */
    }

private:
    base::sptr   _subdev;
    type_t       _type;

    //forward the get calls to the rx or tx
    void get(const wax::type &key, wax::type &val){
        switch(_type){
        case RX_TYPE: return _subdev->rx_get(key, val);
        case TX_TYPE: return _subdev->tx_get(key, val);
        }
    }

    //forward the set calls to the rx or tx
    void set(const wax::type &key, const wax::type &val){
        switch(_type){
        case RX_TYPE: return _subdev->rx_set(key, val);
        case TX_TYPE: return _subdev->tx_set(key, val);
        }
    }
};

/***********************************************************************
 * dboard manager methods
 **********************************************************************/
static manager::dboard_ctor_t const& get_dboard_ctor(
    manager::dboard_id_t dboard_id,
    std::string const& xx_type
){
    //verify that there is a registered constructor for this id
    if (id_to_ctor_map.count(dboard_id) == 0){
        throw std::runtime_error(str(
            boost::format("Unknown %s dboard id: 0x%04x") % xx_type % dboard_id
        ));
    }
    //return the dboard constructor for this id
    return id_to_ctor_map[dboard_id];
}

manager::manager(
    dboard_id_t rx_dboard_id,
    dboard_id_t tx_dboard_id,
    interface::sptr dboard_interface
){
    register_internal_dboards(); //always call first
    const dboard_ctor_t rx_dboard_ctor = get_dboard_ctor(rx_dboard_id, "rx");
    const dboard_ctor_t tx_dboard_ctor = get_dboard_ctor(tx_dboard_id, "tx");
    //make xcvr subdevs (make one subdev for both rx and tx dboards)
    if (rx_dboard_ctor == tx_dboard_ctor){
        for (size_t i = 0; i < ctor_to_num_map[rx_dboard_ctor]; i++){
            base::sptr xcvr_dboard = rx_dboard_ctor(
                base::ctor_args_t(i, dboard_interface)
            );
            _rx_dboards.push_back(xcvr_dboard);
            _tx_dboards.push_back(xcvr_dboard);
        }
    }
    //make tx and rx subdevs (separate subdevs for rx and tx dboards)
    else{
        //make the rx subdevs
        for (size_t i = 0; i < ctor_to_num_map[rx_dboard_ctor]; i++){
            _rx_dboards.push_back(rx_dboard_ctor(
                base::ctor_args_t(i, dboard_interface)
            ));
        }
        //make the tx subdevs
        for (size_t i = 0; i < ctor_to_num_map[tx_dboard_ctor]; i++){
            _tx_dboards.push_back(tx_dboard_ctor(
                base::ctor_args_t(i, dboard_interface)
            ));
        }
    }
}

manager::~manager(void){
    /* NOP */
}

size_t manager::get_num_rx_subdevs(void){
    return _rx_dboards.size();
}

size_t manager::get_num_tx_subdevs(void){
    return _tx_dboards.size();
}

wax::obj::sptr manager::get_rx_subdev(size_t subdev_index){
    return wax::obj::sptr(new subdev_proxy(
        _rx_dboards.at(subdev_index), subdev_proxy::RX_TYPE)
    );
}

wax::obj::sptr manager::get_tx_subdev(size_t subdev_index){
    return wax::obj::sptr(new subdev_proxy(
        _tx_dboards.at(subdev_index), subdev_proxy::TX_TYPE)
    );
}