#include "TreeDevice.h"
#include "anari/type_utility.h"

#include <iostream>

namespace anari_sdk {
namespace tree {

template<int T>
struct param_printer {
    void operator()(ParameterBase &param) {
        typename anari::ANARITypeProperties<T>::array_type data;
        param.get(T, data);
        int components = anari::ANARITypeProperties<T>::components;
        std::cout << anari::ANARITypeProperties<T>::enum_name << ' ';
        if(components>1) {
            std::cout << '(';
            std::cout << data[0];
            for(int i = 1;i<components;++i) {
                std::cout << ", " << data[i];
            }
            std::cout << ')';
        } else {
            std::cout << data[0];
        }
    }
};

static void printParam(ParameterBase &param) {
    if(param) {
        anari::anariTypeInvoke<void, param_printer>(param.type(), param);
    } else {
        std::cout << "nil";
    }
}

static void recursivePrint(TreeDevice *d, ANARIObject obj, int depth = 0) {
    ObjectBase *o = d->fromHandle<ObjectBase*>(obj);

    // get some parameter info
    auto &params = o->parameters();
    size_t count = params.paramCount();
    const char ** paramnames = params.paramNames();

    // print object name
    for(int j = 0;j<depth;++j) { std::cout << "   "; }
    std::cout << anari::toString(o->type()) << ' ';
    if(o->subtype()) {
        std::cout << o->subtype() << ' ';
    }
    std::cout << obj << '\n';

    // iterate over parameters
    for(size_t i = 0;i<count;++i) {
        auto &param = params[i];
        if(param) {
            for(int j = 0;j<depth;++j) { std::cout << "   "; }
            std::cout << "- " << paramnames[i] << ' ';
            printParam(param);
            std::cout << '\n';
        }
        // the parameter holds a h andle descend into that object
        if(anari::isObject(param.type())) {
            recursivePrint(d, param.getHandle(), depth + 1);
        }
    }

    // if this is an array holding objects descend into those too
    if(Object<Array1D> *array = dynamic_cast<Object<Array1D>*>(o)) {
        auto &objects = array->objects();
        for(auto handle : objects) {
            recursivePrint(d, handle, depth + 1);
        }
    }
}

}
}