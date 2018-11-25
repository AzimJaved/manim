from collections import OrderedDict
from mobject.components.edge import Edge
from mobject.components.node import Node
from mobject.mobject import Group
from utils.simple_functions import update_without_overwrite
import constants
import numpy as np
import sys


class Graph(Group):
    CONFIG = {
        "stroke_width": 2,
        "scale_factor": 1,
        "rectangular_stem_width": 0.03,
        "color": constants.BLACK,
    }

    def __init__(self, nodes, edges, attrs=None, **kwargs):
        # typechecking
        for node in nodes:
            Node.assert_primitive(node)
        for edge in edges:
            Edge.assert_primitive(edge)

        if attrs is None:
            attrs = OrderedDict()

        # mobject init
        update_without_overwrite(kwargs, self.CONFIG)
        Group.__init__(self, **kwargs)

        # create submobjects
        self.nodes = {}
        self.edges = {}

        # create nodes
        for point in nodes:
            node_attrs = attrs.get(point, OrderedDict())
            node = Node(point, **update_without_overwrite(node_attrs, kwargs))
            self.nodes[node.key] = node
            self.add(node)

        # create edges
        for pair in edges:
            edge_attrs = attrs.get(pair, OrderedDict())
            u, v = pair[0], pair[1]
            edge_attrs["curved"] = (v, u) in edges
            u = self.nodes[u]
            v = self.nodes[v]
            edge = Edge(u, v, **update_without_overwrite(edge_attrs, kwargs))
            self.edges[edge.key] = edge
            self.add(edge)

    def update_component(self, key, dic, animate=True):
        return self.update_components(
            OrderedDict([(key, dic)]),
            animate=animate,
        )

    def update_components(self, dic, animate=True):
        anims = []
        neighbors_to_update = set()
        for key in dic.keys():
            if key in self.nodes:
                Node.assert_primitive(key)
                anims.extend(self.nodes[key].update_attrs(
                    dic.get(key, None),
                    animate=animate,
                ))
                # update adjacent edges in case radius changes
                for pair in self.get_incident_edges(key, use_direction=False):
                    if pair not in dic and pair not in neighbors_to_update:
                        neighbors_to_update.add(pair)
            elif key in self.edges:
                Edge.assert_primitive(key)
                anims.extend(self.edges[key].update_attrs(
                    dic.get(key, None),
                    animate=animate,
                ))
            else:
                print("Unexpected key {}".format(key), file=sys.stderr)
                breakpoint(context=7)
        for pair in neighbors_to_update:
            anims.extend(self.edges[pair].update_attrs(animate=animate))
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

    def get_edge_label(self, pair, name):
        Edge.assert_primitive(pair)
        return self.edges[pair].get_label(name)

    def edge_has_label(self, pair, label):
        Edge.assert_primitive(pair)
        return label in self.edges[pair].labels

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
        for edge in self.get_incident_edges(point):
            (u, v) = edge
            if u == point:
                adjacent_nodes.append(v)
            else:
                adjacent_nodes.append(u)
        return adjacent_nodes

    def get_incident_edges(self, point, use_direction=True):
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

    def add_edge_updaters(self):
        def follow_endpoint_updater(edge):
            vec = edge.end_node.mobject.get_center() \
                - edge.start_node.mobject.get_center()
            vec = vec / np.linalg.norm(vec)
            start = edge.start_node.mobject.get_center() \
                + vec * edge.start_node.mobject.radius \
                * edge.start_node.scale_factor
            end = edge.end_node.mobject.get_center() \
                - vec * edge.end_node.mobject.radius \
                * edge.end_node.scale_factor
            # if edge.key == ((3.7, 0, 0), (3.7, -3.5, 0)):
            #     print(start, end)
            edge.mobject.put_start_and_end_on(start, end)
        # add radius change, draw nodes first
        for edge in self.edges.values():
            edge.add_updater(follow_endpoint_updater)
