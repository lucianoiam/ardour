/*
 * Copyright (C) 2020 Luciano Iam <lucianito@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ardour/plugin_insert.h"

#include "dispatcher.h"
#include "ardour_websockets.h"
#include "state.h"

using namespace ARDOUR;

#define NODE_METHOD_PAIR(x) { Node::x, &WebsocketsDispatcher::x ## _handler }

WebsocketsDispatcher::NodeMethodMap
WebsocketsDispatcher::_node_to_method = {
    NODE_METHOD_PAIR(tempo),
    NODE_METHOD_PAIR(strip_gain),
    NODE_METHOD_PAIR(strip_pan),
    NODE_METHOD_PAIR(strip_mute),
    NODE_METHOD_PAIR(strip_plugin_enable),
    NODE_METHOD_PAIR(strip_plugin_param_value)
};

void
WebsocketsDispatcher::dispatch (Client client, const NodeStateMessage& msg)
{
    NodeMethodMap::iterator it = _node_to_method.find (msg.state ().node ());
    if (it != _node_to_method.end ()) {
        try {
            (this->*it->second) (client, msg);
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
        }
    }
}

void
WebsocketsDispatcher::update_all_nodes (Client client)
{
    update (client, Node::tempo, {}, { globals ().tempo () });

    for (uint32_t strip_n = 0; strip_n < strips ().strip_count (); ++strip_n) {
        boost::shared_ptr<Stripable> strip = strips ().nth_strip (strip_n);
        boost::shared_ptr<Route> route = boost::dynamic_pointer_cast<Route> (strip);
        if (!route) {
            continue;
        }

        update (client, Node::strip_desc, { strip_n }, { strip->name () });
        update (client, Node::strip_gain, { strip_n }, { strips ().strip_gain (strip_n) });
        update (client, Node::strip_pan, { strip_n }, { strips ().strip_pan (strip_n) });
        update (client, Node::strip_mute, { strip_n }, { strips ().strip_mute (strip_n) });

        for (uint32_t plugin_n = 0 ; ; ++plugin_n) {
            boost::shared_ptr<PluginInsert> insert = strips ()
                .strip_plugin_insert (strip_n, plugin_n);
            if (!insert) {
                break;
            }

            boost::shared_ptr<Plugin> plugin = insert->plugin ();
            update (client, Node::strip_plugin_desc, { strip_n, plugin_n },
                { static_cast<std::string>(plugin->name ()) });

            update (client, Node::strip_plugin_enable, { strip_n, plugin_n },
                { strips ().strip_plugin_enabled (strip_n, plugin_n) });

            for (uint32_t param_n = 0; param_n < plugin->parameter_count (); ++param_n) {
                boost::shared_ptr<AutomationControl> a_ctrl =
                    strips ().strip_plugin_param_control (strip_n, plugin_n, param_n);
                if (!a_ctrl) {
                    continue;
                }

                // possible flags: enumeration, integer_step, logarithmic, sr_dependent, toggled
                ParameterDescriptor pd = a_ctrl->desc ();

                if (pd.toggled) {
                    update (client, Node::strip_plugin_param_desc, { strip_n, plugin_n, param_n },
                        { a_ctrl->name (), std::string("b") });
                } else if (pd.enumeration || pd.integer_step) {
                    update (client, Node::strip_plugin_param_desc, { strip_n, plugin_n, param_n },
                        { a_ctrl->name (), std::string("i"), pd.lower, pd.upper, pd.integer_step });
                } else {
                    update (client, Node::strip_plugin_param_desc, { strip_n, plugin_n, param_n },
                    { a_ctrl->name (), std::string("d"), pd.lower, pd.upper, pd.logarithmic });
                }

                TypedValue value = strips ().strip_plugin_param_value (strip_n, plugin_n, param_n);
                update (client, Node::strip_plugin_param_value, { strip_n, plugin_n, param_n },
                    { value });
            }
        }
    }
}

void
WebsocketsDispatcher::tempo_handler (Client client, const NodeStateMessage& msg)
{
    if (msg.is_write ()) {
        globals ().set_tempo (msg.state ().nth_val (0));
    } else {
        update (client, Node::tempo, {}, { globals ().tempo () });
    }
}

void
WebsocketsDispatcher::strip_gain_handler (Client client, const NodeStateMessage& msg)
{
    uint32_t strip_id = msg.state ().nth_addr (0);

    if (msg.is_write ()) {
        strips ().set_strip_gain (strip_id, msg.state ().nth_val (0));
    } else {
        update (client, Node::strip_gain, { strip_id }, { strips ().strip_gain (strip_id) });
    }
}

void
WebsocketsDispatcher::strip_pan_handler (Client client, const NodeStateMessage& msg)
{
    uint32_t strip_id = msg.state ().nth_addr (0);

    if (msg.is_write ()) {
        strips ().set_strip_pan (strip_id, msg.state ().nth_val (0));
    } else {
        update (client, Node::strip_pan, { strip_id }, { strips ().strip_pan(strip_id) });
    }
}

void
WebsocketsDispatcher::strip_mute_handler (Client client, const NodeStateMessage& msg)
{
    uint32_t strip_id = msg.state ().nth_addr (0);

    if (msg.is_write ()) {
        strips ().set_strip_mute (strip_id, msg.state ().nth_val (0));
    } else {
        update (client, Node::strip_mute, { strip_id }, { strips ().strip_mute (strip_id) });
    }
}

void
WebsocketsDispatcher::strip_plugin_enable_handler (Client client, const NodeStateMessage& msg)
{
    uint32_t strip_id = msg.state ().nth_addr (0);
    uint32_t plugin_id = msg.state ().nth_addr (1);

    if (msg.is_write ()) {
        strips ().set_strip_plugin_enabled (strip_id, plugin_id, msg.state ().nth_val (0));
    } else {
        update (client, Node::strip_plugin_enable, { strip_id, plugin_id },
            { strips ().strip_plugin_enabled (strip_id, plugin_id) });
    }
}

void
WebsocketsDispatcher::strip_plugin_param_value_handler (Client client, const NodeStateMessage& msg)
{
    uint32_t strip_id = msg.state ().nth_addr (0);
    uint32_t plugin_id = msg.state ().nth_addr (1);
    uint32_t param_id = msg.state ().nth_addr (2);

    if (msg.is_write ()) {
        strips ().set_strip_plugin_param_value (strip_id, plugin_id, param_id,
            msg.state ().nth_val (0));
    } else {
        TypedValue value = strips ().strip_plugin_param_value (strip_id, plugin_id, param_id);
        update (client, Node::strip_plugin_param_value, { strip_id, plugin_id, param_id },
            { value });
    }
}

void
WebsocketsDispatcher::update (Client client, std::string node, std::vector<uint32_t> addr,
    std::vector<TypedValue> val)
{
    server ().update_client (client, { node, addr, val }, true);
}