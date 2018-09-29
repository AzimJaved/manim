from collections import OrderedDict
from mobject.components.edge import Edge
from mobject.components.node import Node
from mobject.mobject import Group
import constants
import copy
import sys


class Graph(Group):
    CONFIG = {
        "stroke_width": 2,
        "scale_factor": 1,
        "rectangular_stem_width": 0.03,
        "color": constants.BLACK,
    }

    def __init__(self, nodes, edges, attrs=OrderedDict(), **kwargs):
        # typechecking
        for node in nodes:
            Node.assert_primitive(node)
        for edge in edges:
            Edge.assert_primitive(edge)

        # prevent changes to attrs
        attrs = copy.deepcopy(attrs)

        # mobject init
        config_copy = self.CONFIG.copy()
        config_copy.update(kwargs)
        kwargs = config_copy
        Group.__init__(self, **config_copy)

        # create submobjects
        self.nodes = {}
        self.edges = {}

        # create nodes
        for point in nodes:
            node = Node(point,
                        attrs=attrs.get(point, OrderedDict()),
                        **kwargs)
            self.nodes[node.key] = node
            self.add(node)

        # create edges
        for pair in edges:
            edge_attrs = attrs.get(pair, OrderedDict())
            u, v = pair[0], pair[1]
            edge_attrs["curved"] = (v, u) in edges
            u = self.nodes[u]
            v = self.nodes[v]
            edge = Edge(u, v, attrs=edge_attrs, **kwargs)
            self.edges[edge.key] = edge
            self.add(edge)

    def update_component(self, key, dic):
        return self.update_components(OrderedDict([(key, dic)]))

    def update_components(self, dic):
        anims = []
        neighbors_to_update = set()
        for key in dic.keys():
            if key in self.nodes:
                Node.assert_primitive(key)
                anims.extend(self.nodes[key].update_attrs(dic.get(key, None)))
                # update adjacent edges in case radius changes
                for pair in self.get_adjacent_edges(key, use_direction=False):
                    if pair not in dic and pair not in neighbors_to_update:
                        neighbors_to_update.add(pair)
            elif key in self.edges:
                Edge.assert_primitive(key)
                anims.extend(self.edges[key].update_attrs(dic.get(key, None)))
            else:
                print("Unexpected key {}".format(key), file=sys.stderr)
                breakpoint(context=7)
        for pair in neighbors_to_update:
            anims.extend(self.edges[pair].update_attrs())
        return anims

    def set_labels(self, dic):
        anims = []
        for key in dic.keys():
            if key in self.nodes:
                Node.assert_primitive(key)
                anims.append(self.nodes[key].set_labels(dic[key]))
            elif key in self.edges:
                Edge.assert_primitive(key)
                anims.append(self.edges[key].set_labels(dic[key]))
            else:
                print("Unexpected key {}".format(key), file=sys.stderr)
                breakpoint(context=7)
        return anims

    def get_node_label(self, point, name):
        Node.assert_primitive(point)
        return self.nodes[point].get_label(name)

    def node_has_label(self, point, label):
        Node.assert_primitive(point)
        return label in self.nodes[point].labels

    def get_edge_weight(self, pair):
        Edge.assert_primitive(pair)
        weight = self.edges[pair].get_label("weight")
        if weight:
            return weight.number

    def get_edge(self, pair):
        Edge.assert_primitive(pair)
        return self.edges[pair]

    def get_node(self, point):
        Node.assert_primitive(point)
        return self.nodes[point]

    def get_nodes(self):
        return list(self.nodes.keys())

    def get_edges(self):
        return list(self.edges.keys())

    def get_adjacent_nodes(self, point):
        Node.assert_primitive(point)
        adjacent_nodes = []
        for edge in self.get_adjacent_edges(point):
            (u, v) = edge
            if u == point:
                adjacent_nodes.append(v)
            else:
                adjacent_nodes.append(u)
        return adjacent_nodes

    def get_adjacent_edges(self, point, use_direction=True):
        Node.assert_primitive(point)
        adjacent_edges = []
        for edge in self.edges.keys():
            if edge in adjacent_edges:
                continue
            (u, v) = edge
            if use_direction and self.edges[edge].mobject.directed:
                if u == point:
                    adjacent_edges.append(edge)
            else:
                if u == point or v == point:
                    adjacent_edges.append(edge)
        return adjacent_edges

    def get_opposite_node(self, pair, point):
        Node.assert_primitive(point)
        Edge.assert_primitive(pair)
        return self.edges[pair].opposite(point)

    def set_node_parent_edge(self, point, pair):
        Node.assert_primitive(point)
        Edge.assert_primitive(pair)
        self.nodes[point].parent_edge = pair

    def get_node_parent_edge(self, point):
        return self.nodes[point].get_parent_edge()
